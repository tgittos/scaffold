# Cosmopolitan Python Build

.PHONY: all clean

all: python.com

python.com: build.py Setup.local
	uv run python build.py

clean:
	-$(MAKE) -C build/cpython-3.12.3 clean
	rm -rf build/build-x86_64 build/build-aarch64
	rm -f python.com python.com.x86_64 python.com.aarch64

distclean: clean
	rm -rf __pycache__/
	rm -rf build/
	find . -name "*.pyc" -delete