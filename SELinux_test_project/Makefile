CC = gcc
CFLAGS = -Wall -Wextra -I.
LDFLAGS = -lpthread

all: immutable_service immutable_client

immutable_service: immutable_service.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

immutable_client: immutable_client.c
	$(CC) $(CFLAGS) -o $@ $< -DEXAMPLE_MAIN

libimmutable_client.so: immutable_client.c
	$(CC) $(CFLAGS) -shared -fPIC -o $@ $<

install: immutable_service immutable_client libimmutable_client.so
	install -m 755 -d $(DESTDIR)/usr/local/bin
	install -m 755 -d $(DESTDIR)/usr/local/lib
	install -m 755 -d $(DESTDIR)/usr/local/include
	install -m 755 immutable_service $(DESTDIR)/usr/local/bin/
	install -m 755 immutable_client $(DESTDIR)/usr/local/bin/
	install -m 644 libimmutable_client.so $(DESTDIR)/usr/local/lib/
	install -m 644 immutable_client.h $(DESTDIR)/usr/local/include/

clean:
	rm -f immutable_service immutable_client libimmutable_client.so *.o

# 编译SELinux策略模块
policy:
	checkmodule -M -m -o immutable_policy.mod immutable_policy.te
	semodule_package -o immutable_policy.pp -m immutable_policy.mod
	sudo semodule -i immutable_policy.pp

# 卸载SELinux策略模块
uninstall-policy:
	sudo semodule -r immutable_test

# 设置服务
setup-service:
	sudo cp immutable_service.service /etc/systemd/system/
	sudo systemctl daemon-reload
	sudo systemctl enable immutable_service.service
	sudo systemctl start immutable_service.service

# 启动本地测试环境
test-env: all
	mkdir -p test_dir
	sudo mkdir -p /var/lib/immutable_service
	sudo ./immutable_service &
	sleep 1
	./immutable_client modify test_dir/test.txt "Hello, Immutable World!"
	./immutable_client setretention test_dir/test.txt 3600

.PHONY: all clean install policy uninstall-policy setup-service test-env 