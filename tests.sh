#TEST_NGINX_BINARY=$(pwd)/nginx/objs/nginx prove -v t/ 2>&1

# with apps:
TEST_NGINX_BINARY=$(pwd)/nginx/objs/nginx prove -v t/ js_com_apps/admin_snapshot_rollback/t/ 2>&1
