# ezlib

Native zlib driver for Erlang / Elixir. This library focuses on
compression / decompression of data streams.

## Building

ezlib library can be build as follow:

    ./configure && make

ezlib is a rebar-compatible OTP application. Alternatively, you can
build it with rebar:

    rebar compile

## Dependencies

ezlib library depends on [zlib](http://www.zlib.net/).

You can use CFLAGS, CPPFLAGS and LDFLAGS to pass custom path to zlib
library.

## Usage

You can start ezlib with the following command:

```shell
$ erl -pa ebin
Erlang/OTP 17 [erts-6.3] [source] [64-bit] [smp:4:4] [async-threads:10] [hipe] [kernel-poll:false] [dtrace]

Eshell V6.3  (abort with ^G)

% Start the application:
1> application:start(ezlib).
```

## Development

### Test

#### Unit test

You can run eunit test with the command:

    $ rebar eunit

