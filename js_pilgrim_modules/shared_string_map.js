/*
 * SharedStringMap — synchronous, cross-worker string key-value store
 * backed by a pre-fork SharedArrayBuffer.
 *
 * Drop-in ergonomic replacement for nginx.shared with full SAB properties:
 * synchronous access, no central bottleneck, Atomics-based spinlock,
 * and interoperable with SharedWorker (pass .sab via postMessage).
 *
 * Usage:
 *   // init_conf / top of js_source:
 *   const store = new SharedStringMap(256, 128, 512);
 *
 *   // any worker, synchronously:
 *   store.set('feature.dark_mode', 'true');
 *   store.get('feature.dark_mode');   // → 'true'
 *   store.delete('feature.dark_mode');
 *   store.has('feature.dark_mode');   // → false
 *   store.size;                       // → current entry count
 *   store.clear();
 *
 * SAB layout:
 *   [Int32 lock][Int32 count][slot × capacity]
 *   slot = [u8 used][u32 hash (LE)][u8×maxKeyBytes key][u8×maxValBytes val]
 *   used: 0=empty  1=occupied  2=tombstone
 *
 * Constraints (same as nginx.shared):
 *   - capacity must be a power of two
 *   - keys and values are silently truncated to maxKeyBytes / maxValBytes
 *   - keys are UTF-8; binary keys with embedded NUL are not supported
 *   - load factor above ~70 % degrades performance; size never exceeds capacity
 */

const EMPTY     = 0;
const OCCUPIED  = 1;
const TOMBSTONE = 2;

const HDR_BYTES  = 8;  // Int32 lock + Int32 count
const META_BYTES = 5;  // u8 used + u32 hash

function fnv32a(bytes) {
    let h = 0x811c9dc5;
    for (let i = 0; i < bytes.length; i++) {
        h = Math.imul(h ^ bytes[i], 0x01000193) >>> 0;
    }
    return h;
}

// Pure-JS UTF-8 encoder — avoids TextEncoder which is not available in
// QuickJS embedded contexts (nginx, SharedWorker).
// Returns a Uint8Array of at most maxLen bytes; multi-byte sequences that
// would overflow are silently dropped so the result is always valid UTF-8.
function _encode(str, maxLen) {
    const out = new Uint8Array(maxLen);
    let pos = 0;
    for (let i = 0; i < str.length; i++) {
        let c = str.charCodeAt(i);
        if (c < 0x80) {
            if (pos + 1 > maxLen) break;
            out[pos++] = c;
        } else if (c < 0x800) {
            if (pos + 2 > maxLen) break;
            out[pos++] = 0xC0 | (c >> 6);
            out[pos++] = 0x80 | (c & 0x3F);
        } else if (c >= 0xD800 && c <= 0xDBFF && i + 1 < str.length) {
            const c2 = str.charCodeAt(i + 1);
            if (c2 >= 0xDC00 && c2 <= 0xDFFF) {
                const cp = 0x10000 + ((c & 0x3FF) << 10) + (c2 & 0x3FF);
                if (pos + 4 > maxLen) break;
                out[pos++] = 0xF0 | (cp >> 18);
                out[pos++] = 0x80 | ((cp >> 12) & 0x3F);
                out[pos++] = 0x80 | ((cp >>  6) & 0x3F);
                out[pos++] = 0x80 |  (cp        & 0x3F);
                i++;
            }
        } else {
            if (pos + 3 > maxLen) break;
            out[pos++] = 0xE0 |  (c >> 12);
            out[pos++] = 0x80 | ((c >>  6) & 0x3F);
            out[pos++] = 0x80 |  (c        & 0x3F);
        }
    }
    return pos < maxLen ? out.subarray(0, pos) : out;
}

// Pure-JS UTF-8 decoder — reads buf[offset..offset+maxLen) until NUL.
function _decode(buf, offset, maxLen) {
    let str = '';
    let i = offset;
    const end = offset + maxLen;
    while (i < end && buf[i] !== 0) {
        const b = buf[i++];
        if (b < 0x80) {
            str += String.fromCharCode(b);
        } else if ((b & 0xE0) === 0xC0) {
            str += String.fromCharCode(((b & 0x1F) << 6) | (buf[i++] & 0x3F));
        } else if ((b & 0xF0) === 0xE0) {
            const b2 = buf[i++], b3 = buf[i++];
            str += String.fromCharCode(((b & 0x0F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F));
        } else {
            const b2 = buf[i++], b3 = buf[i++], b4 = buf[i++];
            const cp = ((b & 0x07) << 18) | ((b2 & 0x3F) << 12) | ((b3 & 0x3F) << 6) | (b4 & 0x3F);
            const c  = cp - 0x10000;
            str += String.fromCharCode(0xD800 + (c >> 10), 0xDC00 + (c & 0x3FF));
        }
    }
    return str;
}

export class SharedStringMap {
    /**
     * @param {number}             capacity    max entries; must be power of two
     * @param {number}             maxKeyBytes max UTF-8 bytes per key
     * @param {number}             maxValBytes max UTF-8 bytes per value
     * @param {SharedArrayBuffer}  [sab]       attach to existing SAB instead of allocating
     */
    constructor(capacity, maxKeyBytes, maxValBytes, sab) {
        if ((capacity & (capacity - 1)) !== 0) {
            throw new RangeError('SharedStringMap: capacity must be a power of two');
        }
        this._cap  = capacity;
        this._klen = maxKeyBytes;
        this._vlen = maxValBytes;
        this._slen = META_BYTES + maxKeyBytes + maxValBytes;
        this._mask = capacity - 1;

        const totalBytes = HDR_BYTES + capacity * this._slen;
        this._sab   = sab || new SharedArrayBuffer(totalBytes);
        this._hdr32 = new Int32Array(this._sab, 0, 2);         // [lock, count]
        this._data  = new Uint8Array(this._sab, HDR_BYTES);    // slot area
    }

    /** The underlying SharedArrayBuffer — pass to postMessage for SharedWorker access. */
    get sab()        { return this._sab; }

    /** Number of live entries. */
    get size()       { return Atomics.load(this._hdr32, 1); }

    /** Total byte length of the backing SAB. */
    get byteLength() { return this._sab.byteLength; }

    // ── Spinlock ──────────────────────────────────────────────────────────────

    _acquire() {
        while (Atomics.compareExchange(this._hdr32, 0, 0, 1) !== 0) { /* spin */ }
    }

    _release() {
        Atomics.store(this._hdr32, 0, 0);
    }

    // ── Slot helpers (caller holds lock) ──────────────────────────────────────

    _off(idx)       { return idx * this._slen; }
    _used(off)      { return this._data[off]; }
    _hash(off) {
        return ( this._data[off + 1]
               | (this._data[off + 2] << 8)
               | (this._data[off + 3] << 16)
               | (this._data[off + 4] << 24)) >>> 0;
    }

    _keyEq(off, kb) {
        const base = off + META_BYTES;
        for (let i = 0; i < kb.length; i++) {
            if (this._data[base + i] !== kb[i]) return false;
        }
        // stored key must end here (no longer key with same prefix)
        return kb.length >= this._klen || this._data[base + kb.length] === 0;
    }

    _writeSlot(off, hash, kb, vb) {
        this._data[off] = OCCUPIED;
        this._data[off + 1] =  hash        & 0xff;
        this._data[off + 2] = (hash >>  8) & 0xff;
        this._data[off + 3] = (hash >> 16) & 0xff;
        this._data[off + 4] = (hash >> 24) & 0xff;
        const kbase = off + META_BYTES;
        this._data.fill(0, kbase, kbase + this._klen);
        this._data.set(kb, kbase);
        const vbase = kbase + this._klen;
        this._data.fill(0, vbase, vbase + this._vlen);
        this._data.set(vb, vbase);
    }

    _readVal(off) {
        return _decode(this._data, off + META_BYTES + this._klen, this._vlen);
    }

    // ── Linear probe ─────────────────────────────────────────────────────────

    // Returns { found, idx, tombIdx }
    _probe(kb, hash) {
        const start = hash & this._mask;
        let tombIdx = -1;
        for (let i = 0; i < this._cap; i++) {
            const idx = (start + i) & this._mask;
            const off = this._off(idx);
            const u   = this._used(off);
            if (u === EMPTY) {
                return { found: false, idx, tombIdx };
            }
            if (u === TOMBSTONE) {
                if (tombIdx === -1) tombIdx = idx;
                continue;
            }
            if (this._hash(off) === hash && this._keyEq(off, kb)) {
                return { found: true, idx, tombIdx };
            }
        }
        // full table scan: no empty, no match
        return { found: false, idx: -1, tombIdx };
    }

    // ── Public API ────────────────────────────────────────────────────────────

    get(key) {
        const kb   = _encode(key, this._klen);
        const hash = fnv32a(kb);
        this._acquire();
        try {
            const { found, idx } = this._probe(kb, hash);
            return found ? this._readVal(this._off(idx)) : undefined;
        } finally {
            this._release();
        }
    }

    set(key, value) {
        const kb   = _encode(key, this._klen);
        const vb   = _encode(String(value), this._vlen);
        const hash = fnv32a(kb);
        this._acquire();
        try {
            const { found, idx, tombIdx } = this._probe(kb, hash);
            if (found) {
                // update value in-place; key, hash, used unchanged
                const vbase = this._off(idx) + META_BYTES + this._klen;
                this._data.fill(0, vbase, vbase + this._vlen);
                this._data.set(vb, vbase);
            } else {
                const target = tombIdx !== -1 ? tombIdx : idx;
                if (target === -1) {
                    throw new Error('SharedStringMap: capacity exhausted');
                }
                const isNew = this._used(this._off(target)) !== OCCUPIED;
                this._writeSlot(this._off(target), hash, kb, vb);
                if (isNew) {
                    this._hdr32[1]++;
                }
            }
        } finally {
            this._release();
        }
    }

    delete(key) {
        const kb   = _encode(key, this._klen);
        const hash = fnv32a(kb);
        this._acquire();
        try {
            const { found, idx } = this._probe(kb, hash);
            if (!found) return false;
            this._data[this._off(idx)] = TOMBSTONE;
            this._hdr32[1]--;
            return true;
        } finally {
            this._release();
        }
    }

    has(key) {
        return this.get(key) !== undefined;
    }

    clear() {
        this._acquire();
        try {
            this._data.fill(0);
            this._hdr32[1] = 0;
        } finally {
            this._release();
        }
    }
}
