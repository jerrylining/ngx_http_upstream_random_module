#随机负载均衡
配置样式
	upstream load_balance {
			server ip1 weight=1 fail_timeout=20s;
			server ip2 weight=2 fail_timeout=20s;
			server ip3 weight=2 fail_timeout=20s;
            server ip4 weight=1 fail_timeout=20s;
	random_ups;
}
