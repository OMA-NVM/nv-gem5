all: hello64-static hello32-static

hello64-static: simple_test.c dockcross-arm64
	./dockcross-arm64 bash -c '$$CC simple_test.c -o hello64-static -static'

hello32-static: simple_test.c dockcross-armv7
	./dockcross-armv7 bash -c '$$CC simple_test.c -o hello32-static -static'

dockcross-arm64:
	docker run --rm dockcross/linux-arm64 > ./dockcross-arm64
	chmod +x ./dockcross-arm64

dockcross-armv7:
	docker run --rm dockcross/linux-armv7 > ./dockcross-armv7
	chmod +x ./dockcross-armv7

clean:
	rm -f dockcross* hello64-static hello32-static
