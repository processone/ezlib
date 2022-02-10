%%%----------------------------------------------------------------------
%%% File    : ezlib.erl
%%% Author  : Alexey Shchepin <alexey@process-one.net>
%%% Purpose : Interface to zlib
%%% Created : 19 Jan 2006 by Alexey Shchepin <alexey@process-one.net>
%%%
%%%
%%% Copyright (C) 2002-2022 ProcessOne, SARL. All Rights Reserved.
%%%
%%% Licensed under the Apache License, Version 2.0 (the "License");
%%% you may not use this file except in compliance with the License.
%%% You may obtain a copy of the License at
%%%
%%%     http://www.apache.org/licenses/LICENSE-2.0
%%%
%%% Unless required by applicable law or agreed to in writing, software
%%% distributed under the License is distributed on an "AS IS" BASIS,
%%% WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
%%% See the License for the specific language governing permissions and
%%% limitations under the License.
%%%
%%%----------------------------------------------------------------------

-module(ezlib).

-author('alexey@process-one.net').

-behaviour(gen_server).

-export([start_link/0, enable_zlib/2,
	 disable_zlib/1, send/2, recv/2, recv/3, recv_data/2,
	 setopts/2, sockname/1, peername/1, get_sockmod/1,
	 controlling_process/2, close/1]).

%% Internal exports, call-back functions.
-export([init/1, handle_call/3, handle_cast/2,
	 handle_info/2, code_change/3, terminate/2]).

-export([new/0, new/3, compress/2, decompress/2, load_nif/1]).

-record(zlibsock, {sockmod :: atom(),
		   socket :: inet:socket(),
		   zlibport :: port()}).

-type zlib_socket() :: #zlibsock{}.

-export_type([zlib_socket/0]).

start_link() ->
    gen_server:start_link({local, ?MODULE}, ?MODULE, [],
			  []).

init([]) ->
    case load_nif() of
	ok ->
	    {ok, []};
	{error, Reason} ->
	    {stop, Reason}
    end.

new() ->
    erlang:nif_error(nif_not_loaded).

-spec new(integer(), integer(), integer()) -> port().

new(_CompressionRatio, _Window, _MemLevel) ->
    erlang:nif_error(nif_not_loaded).

compress(_State, _Data) ->
    erlang:nif_error(nif_not_loaded).

decompress(_State, _Data) ->
    erlang:nif_error(nif_not_loaded).

load_nif() ->
    SOPath = p1_nif_utils:get_so_path(?MODULE, [ezlib], "ezlib"),
    load_nif(SOPath).

load_nif(SOPath) ->
    case erlang:load_nif(SOPath, 0) of
	ok ->
	    ok;
	{error, {Reason, Txt}} ->
	    error_logger:error_msg("failed to load NIF ~s: ~s",
				   [SOPath, Txt]),
	    {error, Reason}
    end.

%%% --------------------------------------------------------
%%% The call-back functions.
%%% --------------------------------------------------------

handle_call(_, _, State) -> {noreply, State}.

handle_cast(_, State) -> {noreply, State}.

handle_info({'EXIT', Port, Reason}, Port) ->
    {stop, {port_died, Reason}, Port};
handle_info({'EXIT', _Pid, _Reason}, Port) ->
    {noreply, Port};
handle_info(_, State) -> {noreply, State}.

code_change(_OldVsn, State, _Extra) -> {ok, State}.

terminate(_Reason, _State) ->
    ok.

-spec enable_zlib(atom(), inet:socket()) -> {ok, zlib_socket()} | {error, any()}.

enable_zlib(SockMod, Socket) ->
    Port = ezlib:new(),
    {ok,
     #zlibsock{sockmod = SockMod, socket = Socket,
	       zlibport = Port}}.

-spec disable_zlib(zlib_socket()) -> {atom(), inet:socket()}.

disable_zlib(#zlibsock{sockmod = SockMod,
		       socket = Socket}) ->
    {SockMod, Socket}.

-spec recv(zlib_socket(), number()) -> {ok, binary()} | {error, any()}.

recv(Socket, Length) -> recv(Socket, Length, infinity).

-spec recv(zlib_socket(), number(), timeout()) ->
    {ok, binary()} |
    {error, any()}.

recv(#zlibsock{sockmod = SockMod, socket = Socket} =
     ZlibSock,
     Length, Timeout) ->
    case SockMod:recv(Socket, Length, Timeout) of
	{ok, Packet} -> recv_data(ZlibSock, Packet);
	{error, _Reason} = Error -> Error
    end.

-spec recv_data(zlib_socket(), iodata()) -> {ok, binary()} | {error, any()}.

recv_data(#zlibsock{sockmod = SockMod,
		    socket = Socket} =
	      ZlibSock,
	  Packet) ->
    case SockMod of
	gen_tcp -> recv_data2(ZlibSock, Packet);
	_ ->
	    case SockMod:recv_data(Socket, Packet) of
		{ok, Packet2} -> recv_data2(ZlibSock, Packet2);
		Error -> Error
	    end
    end.

recv_data2(ZlibSock, Packet) ->
    case catch recv_data1(ZlibSock, Packet) of
	{'EXIT', Reason} -> {error, Reason};
	Res -> Res
    end.

recv_data1(#zlibsock{zlibport = Port} = _ZlibSock,
	   Packet) ->
    try ezlib:decompress(Port, Packet)
    catch _:badarg ->
	{error, einval}
    end.

-spec send(zlib_socket(), iodata()) -> ok | {error, binary() | inet:posix()}.

send(#zlibsock{sockmod = SockMod, socket = Socket,
	       zlibport = Port},
     Packet) ->
    try ezlib:compress(Port, Packet) of
	{ok, Compressed} -> SockMod:send(Socket, Compressed);
	{error, _} = Err -> Err
    catch _:badarg ->
	{error, einval}
    end.

-spec setopts(zlib_socket(), list()) -> ok | {error, inet:posix()}.

setopts(#zlibsock{sockmod = SockMod, socket = Socket},
	Opts) ->
    case SockMod of
	gen_tcp -> inet:setopts(Socket, Opts);
	_ -> SockMod:setopts(Socket, Opts)
    end.

-spec sockname(zlib_socket()) ->
    {ok, {inet:ip_address(), inet:port_number()}} |
    {error, inet:posix()}.

sockname(#zlibsock{sockmod = SockMod,
		   socket = Socket}) ->
    case SockMod of
	gen_tcp -> inet:sockname(Socket);
	_ -> SockMod:sockname(Socket)
    end.

-spec get_sockmod(zlib_socket()) -> atom().

get_sockmod(#zlibsock{sockmod = SockMod}) -> SockMod.

-spec peername(zlib_socket()) ->
    {ok, {inet:ip_address(), inet:port_number()}} |
    {error, inet:posix()}.

peername(#zlibsock{sockmod = SockMod,
		   socket = Socket}) ->
    case SockMod of
	gen_tcp -> inet:peername(Socket);
	_ -> SockMod:peername(Socket)
    end.

-spec controlling_process(zlib_socket(), pid()) -> ok | {error, atom()}.

controlling_process(#zlibsock{sockmod = SockMod,
			      socket = Socket},
		    Pid) ->
    SockMod:controlling_process(Socket, Pid).

-spec close(zlib_socket()) -> ok.

close(#zlibsock{sockmod = SockMod, socket = Socket,
		zlibport = Port}) ->
    SockMod:close(Socket),
    catch port_close(Port),
    ok.
