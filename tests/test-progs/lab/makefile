# lab/makefile
CXXFLAGS := -Iinclude -std=gnu++17 -DinCIM=1 -DSTD_PRINT_OUTPUT
LDFLAGS  := -static
# COMPILEFILE := src/desc_bank_test.cpp 
COMPILEFILE := src/desc_on_cim.cpp

all: bin/hello64-static 
#bin/hello32-static

bin/hello64-static: src/cim_api.cpp $(COMPILEFILE) dockcross-arm64
	./dockcross-arm64 bash -lc 'mkdir -p bin && echo "CXX=$$CXX" && $$CXX $(CXXFLAGS) src/cim_api.cpp $(COMPILEFILE) -o bin/hello64-static $(LDFLAGS)'

# bin/hello32-static: src/cim_api.cpp src/simple_test.cpp dockcross-armv7
# 	./dockcross-armv7 bash -lc 'mkdir -p bin && echo "CXX=$$CXX" && $$CXX $(CXXFLAGS) src/cim_api.cpp src/simple_test.cpp -o bin/hello32-static $(LDFLAGS)'

dockcross-arm64:
	docker run --rm dockcross/linux-arm64 > ./dockcross-arm64
	chmod +x ./dockcross-arm64

dockcross-armv7:
	docker run --rm dockcross/linux-armv7 > ./dockcross-armv7
	chmod +x ./dockcross-armv7

clean:
	rm -f dockcross* bin/hello64-static bin/hello32-static