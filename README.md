libvmod-jq
==========

[![ci](https://github.com/fgsch/libvmod-jq/actions/workflows/ci.yml/badge.svg?branch=devel)](https://github.com/fgsch/libvmod-jq/actions/workflows/ci.yml)

## About

A Varnish master VMOD to process JSON input.

## Requirements

To build this VMOD you will need:

* make
* a C compiler, e.g. GCC or clang
* pkg-config
* python3-docutils or docutils in macOS [1]
* Varnish master built from sources
* libjq-dev in recent Debian/Ubuntu releases, jq in macOS [1]. See
  also https://github.com/stedolan/jq

If you are building from Git, you will also need:

* autoconf
* automake
* libtool

In addition, you will need to set `PKG_CONFIG_PATH` to the directory
where **varnishapi.pc** is located before running `autogen.sh` and
`configure`.  For example:

```
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig
```

## Installation

### From a tarball

To install this VMOD, run the following commands:

```
./configure
make
make check
sudo make install
```

The `make check` step is optional but it's good to know whether the
tests are passing on your platform.

### From the Git repository

To install from Git, clone this repository by running:

```
git clone --recursive https://github.com/fgsch/libvmod-jq
```

And then run `./autogen.sh` followed by the instructions above for
installing from a tarball.

## Example

```
import jq;

sub vcl_recv {
	jq.parse(string, "[1, 2, 3]");
	if (jq.get(".[0]", "10") == "1") {
		...
	}
}
```

## License

This VMOD is licensed under BSD license. See LICENSE for details.

### Note

1. Using Homebrew, https://github.com/Homebrew/brew/.
