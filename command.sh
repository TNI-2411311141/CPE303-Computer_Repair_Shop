do_nginx() {
	nginx_pid="$(grep -E '^pid' nginx.conf | cut -d ' ' -f2-)"
	nginx_pid="${nginx_pid%\;}"
	our_nginx() {
		nginx -p . -c nginx.conf -e nginx/error.log $@
	}
	case "$1" in
		"start")
			do_nginx test
			while [ -f "$nginx_pid" ]; do
				echo 'nginx pid exist, waiting for termination'
				sleep 0.5
			done
			echo 'starting nginx'
			our_nginx
			;;
		"stop")
			echo 'stopping nginx'
			our_nginx -s quit
			;;
		"reload")
			echo 'reloading nginx'
			our_nginx -s reload
			;;
		"test")
			if [ -f "$nginx_pid" ]; then
				our_nginx -t || exit 1
			else
				our_nginx -t || exit 1
				rm "$nginx_pid"
			fi
			;;
		"restart")
			do_nginx stop
			do_nginx start
			;;
	esac
}

case "$1" in
	'nginx') do_nginx $2;;
esac
