SMCP — ReadMe
=============

SMCP is a C-based CoAP stack which is suitable for embedded environments.
Features include:

 * Supports RFC7252 <http://tools.ietf.org/html/rfc7252>.
 * Fully asynchronous I/O.
 * Supports both BSD sockets and [µIP](http://en.wikipedia.org/wiki/UIP_(micro_IP%29).
 * Sending and receiving asynchronous CoAP responses.
 * Observing resources and offering observable resources.
 * Retransmission of confirmable transactions.
 * `smcpctl` - a powerful command line tool for browsing and configuring CoAP nodes.

Initial focus is on correctness of implementation. Stack usage and other
performance optimizations will become the focus later on.

## Why is it called *SMCP*? ##

Historical reasons. Don't think about it too much.

## Getting Help ##

If you are having trouble with SMCP, you can join the official SMCP mailing
list and ask your questions there.

* [SMCP Developers Group](https://groups.google.com/group/smcp-dev) <smcp-dev@googlegroups.com>

## Getting, building, and installing via Git ##

First:

	$ git clone git://github.com/darconeous/smcp.git
	$ cd smcp

To just build the latest tagged stable release:

	$ git checkout latest-release
	$ ./configure
	$ make
	$ sudo make install

For bleeding-edge:

	$ git checkout master
	$ git archive origin/master-autoconf | tar xv # Or you should run `./bootstrap.sh`
	$ ./configure
	$ make
	$ sudo make install

## Getting, building, and installing from an archive ##

	$ curl https://github.com/darconeous/smcp/archive/latest-release.zip > smcp-latest-release.zip
	$ unzip latest-release.zip
	$ cd smcp-latest-release
	$ ./configure
	$ make
	$ sudo make install	

## Installing via Homebrew on OS X ##

To get the "latest-release":

	$ brew tap darconeous/embedded
	$ brew install smcp

To get the bleeding-edge release:

	$ brew tap darconeous/embedded
	$ brew install smcp --HEAD

Node: This is mostly for people who just want to use `smcpctl` (described below).
If you want to compile against SMCP, you'll currently need to grab the sources
and build against them directly.

## Getting Started ##

The best way to get started is to have a look at some example code
which uses SMCP. The best example at the moment is the plugtest server,
which is in the following files:

 * `src/plugtest/main-server.c` - Just the main run-loop
 * `src/plugtest/plugtest-server.c` - All of the good stuff is in here.
 * `src/plugtest/plugtest-server.h`

The Contiki version of the plugtest uses the last two files. You can find
the Contiki version at `contiki-src/examples/smcp-plugtest/`.

## Configurability ##

One of the goals of SMCP is to implent a full-featured CoAP library, but
most embedded applications don't need all of these capabilities. Because of this,
SMCP is designed so that you can individually enable or disable features
depending on your needs (See `src/smcp/smcp-opts.h`).

For example, SMCP has the ability to have more than once instance, but embedded
platforms will never need more than one. Passing around a reference to a
global variable that will never change is wasteful, so when compiled with
`SMCP_EMBEDDED` turned on, we transparently (via some preprocessor magic) ignore
the reference to the SMCP instance from all of the functions that take it.
This makes it easy to use the same codebase for both embedded and non-embedded
applications. There are other configuration options for doing things like
limiting `malloc()` usage, avoiding use of `printf()` (and variants),
enabling/disabling observing, etc.

## Contiki Support ##

SMCP fully supports [Contiki](http://contiki-os.org/). To build the Contiki
examples, just make sure that the `CONTIKI` environment variable is set point
to your Contiki root, like so:

	$ cd contiki-src/examples/smcp-simple
	$ make CONTIKI=~/Projects/contiki TARGET=minimal-net

## API Documentation ##

You can find an online version of the API documentation here:
<http://darconeous.github.com/smcp/doc/html/>

## SMCPCTL ##

`smcpctl` is a command-line interface for browsing, observing, and
interacting with CoAP devices. It is, for the most part, self-
documenting: just type in `smcpctl help`. You can run individual
commands directly from the command line when invoking `smcpctl` or you can invoke
with no arguments and you will enter the smcpctl shell (CLI). The
shell environment allows you to use familiar unix commands like
`ls`, `cd`, and `cat`. The CLI supports quoting and tab-completion of resource
names, which is incredibly handy.

Here are a few examples of how you can use it:

### GET a resource ###

	$ smcpctl get coap://coap.me/large

### Listing the contents of a resource ###

	$ smcpctl ls coap://coap.me/.well-known/core

### PUT a resource and show parsed response headers ###

	$ smcpctl put -i coap://coap.me/test "Testing out smcpctl's PUT command"

### Observe a resource for changes ###

	$ smcpctl observe coap://vs0.inf.ethz.ch/obs

## SMCPD ##

`smcpd` is a CoAP daemon that is eventually intended to be a flexible
CoAP server for Linux and other unix-like machines.
It is a work in progress.

## Plugtests ##

`smcp-plugtest-server` implements some of the ESTI plugtests for CoAP.
It is a work in progress.

## List of Test Servers ##

 * <coap://coap.me/>
 * <coap://vs0.inf.ethz.ch/>

## Authors and Contributors ##

 * Robert Quattlebaum <darco@deepdarc.com>
