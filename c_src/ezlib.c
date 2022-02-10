/*
 * Copyright (C) 2002-2022 ProcessOne, SARL. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <stdio.h>
#include <string.h>
#include <erl_nif.h>
#include <zlib.h>

#define BUF_SIZE 1024

typedef struct ezlib_state {
    z_stream *d_stream;
    z_stream *i_stream;
} ezlib_state_t;

static void destroy_ezlib_state(ErlNifEnv *env, void *data) {
    ezlib_state_t *state = (ezlib_state_t *) data;
    if (!state)
        return;

    if (state->d_stream) {
        deflateEnd(state->d_stream);
        enif_free(state->d_stream);
    }
    if (state->i_stream) {
        inflateEnd(state->i_stream);
        enif_free(state->i_stream);
    }
    memset(state, 0, sizeof(ezlib_state_t));
}

static ErlNifResourceType *resource_type = NULL;

static int load(ErlNifEnv *env, void **priv, ERL_NIF_TERM load_info) {
    ErlNifResourceFlags flags = ERL_NIF_RT_CREATE | ERL_NIF_RT_TAKEOVER;
    resource_type = enif_open_resource_type(env, NULL, "ezlib_state",
                                            destroy_ezlib_state,
                                            flags, NULL);

    return 0;
}

static int upgrade(ErlNifEnv *env, void **priv, void **old_priv, ERL_NIF_TERM load_info) {
    return load(env, priv, load_info);
}

static ERL_NIF_TERM make_exception(ErlNifEnv *env, char *atom) {
    return enif_raise_exception(env, enif_make_atom(env, atom));
}

static ERL_NIF_TERM make_error(ErlNifEnv *env, char *atom) {
    return enif_make_tuple2(env, enif_make_atom(env, "error"),
                            enif_make_atom(env, atom));
}

static ERL_NIF_TERM make_result(ErlNifEnv *env, ErlNifBinary *bin) {
    return enif_make_tuple2(env, enif_make_atom(env, "ok"),
                            enif_make_binary(env, bin));
}

static void *zlib_alloc(void *data, unsigned int items, unsigned int size) {
    return (void *) enif_alloc(items * size);
}

static void zlib_free(void *data, void *addr) {
    enif_free(addr);
}

static ERL_NIF_TERM allocate(ErlNifEnv *env, int ratio, int window, int memLevel) {
    z_stream *i_stream = (z_stream *) enif_alloc(sizeof(z_stream));
    if (!i_stream)
        return make_exception(env, "enomem");

    z_stream *d_stream = (z_stream *) enif_alloc(sizeof(z_stream));
    if (!d_stream) {
        enif_free(i_stream);
        return make_exception(env, "enomem");
    }

    ezlib_state_t *state = enif_alloc_resource(resource_type, sizeof(ezlib_state_t));
    if (!state) {
        enif_free(i_stream);
        enif_free(d_stream);
        return make_exception(env, "enomem");
    }

    i_stream->zalloc = zlib_alloc;
    i_stream->zfree = zlib_free;
    i_stream->opaque = (voidpf) 0;
    inflateInit(i_stream);
    state->i_stream = i_stream;

    d_stream->zalloc = zlib_alloc;
    d_stream->zfree = zlib_free;
    d_stream->opaque = (voidpf) 0;
    deflateInit2(d_stream, ratio, Z_DEFLATED, window, memLevel, Z_DEFAULT_STRATEGY);
    state->d_stream = d_stream;

    ERL_NIF_TERM result = enif_make_resource(env, state);
    enif_release_resource(state);

    return result;
}

static ERL_NIF_TERM new_with_params_nif(ErlNifEnv *env, int argc,
                                        const ERL_NIF_TERM argv[]) {
    int ratio, window, memLevel;

    if (argc != 3)
        return enif_make_badarg(env);

    if (!enif_get_int(env, argv[0], &ratio) || ratio < 0 || ratio > 9)
        return enif_make_badarg(env);
    if (!enif_get_int(env, argv[1], &window) || window < 8 || window > 15)
        return enif_make_badarg(env);
    if (!enif_get_int(env, argv[2], &memLevel) || memLevel < 1 || memLevel > 8)
        return enif_make_badarg(env);

    return allocate(env, ratio, window, memLevel);
}

static ERL_NIF_TERM new_nif(ErlNifEnv *env, int argc,
                            const ERL_NIF_TERM argv[]) {
    if (argc != 0)
        return enif_make_badarg(env);

    return allocate(env, Z_DEFAULT_COMPRESSION, 12, 4);
}

static ERL_NIF_TERM decompress_nif(ErlNifEnv *env, int argc,
                                   const ERL_NIF_TERM argv[]) {
    ezlib_state_t *state;
    ErlNifBinary bin;
    ErlNifBinary result;

    if (argc != 2)
        return enif_make_badarg(env);
    if (!enif_get_resource(env, argv[0], resource_type, (void *) &state))
        return enif_make_badarg(env);

    if (!enif_inspect_binary(env, argv[1], &bin))
        return enif_make_badarg(env);

    if (!enif_alloc_binary(BUF_SIZE, &result)) {
        return make_error(env, "enomem");
    }
    z_stream *i_stream = state->i_stream;
    i_stream->next_in = bin.data;
    i_stream->avail_in = bin.size;
    int err, offset = 0;
    do {
        i_stream->avail_out = result.size - offset;
        i_stream->next_out = (unsigned char *) result.data + offset;

        err = inflate(i_stream, Z_SYNC_FLUSH);

        if ((err == Z_BUF_ERROR && i_stream->avail_out == BUF_SIZE) ||
            (err == Z_OK && i_stream->avail_out > 0)) {
            break;
        } else if (err == Z_OK && i_stream->avail_out == 0) {
            offset += BUF_SIZE;
            if (!enif_realloc_binary(&result, result.size + BUF_SIZE)) {
                return make_error(env, "enomem");
            }
        } else if (err == Z_OK && i_stream->avail_out > 0) {
            break;
        } else {
            enif_release_binary(&result);
            if (err == Z_MEM_ERROR)
                return make_error(env, "enomem");
            return make_error(env, "einval");
        }
    } while (1);

    if (!enif_realloc_binary(&result, result.size - i_stream->avail_out)) {
        return make_error(env, "enomem");
    }
    return make_result(env, &result);
}

static int min(int a, int b) {
    return a > b ? b : a;
}
static ERL_NIF_TERM compress_nif(ErlNifEnv *env, int argc,
                                 const ERL_NIF_TERM argv[]) {
    ezlib_state_t *state;
    ErlNifBinary bin;
    ErlNifBinary result;

    if (argc != 2)
        return enif_make_badarg(env);
    if (!enif_get_resource(env, argv[0], resource_type, (void *) &state))
        return enif_make_badarg(env);

    if (!enif_inspect_binary(env, argv[1], &bin))
        return enif_make_badarg(env);

    if (!enif_alloc_binary(min(BUF_SIZE, bin.size + 8), &result)) {
        return make_error(env, "enomem");
    }
    z_stream *d_stream = state->d_stream;
    d_stream->next_in = bin.data;
    d_stream->avail_in = bin.size;
    int err, offset = 0;
    do {
        d_stream->avail_out = result.size - offset;
        d_stream->next_out = (unsigned char *) result.data + offset;

        err = deflate(d_stream, Z_SYNC_FLUSH);

        if ((err == Z_BUF_ERROR && d_stream->avail_out == BUF_SIZE) ||
            (err == Z_OK && d_stream->avail_out > 0)) {
            break;
        } else if (err == Z_OK && d_stream->avail_out == 0) {
            offset += BUF_SIZE;
            if (!enif_realloc_binary(&result, result.size + BUF_SIZE)) {
                return make_error(env, "enomem");
            }
        } else if (err == Z_OK && d_stream->avail_out > 0) {
            break;
        } else {
            enif_release_binary(&result);
            if (err == Z_MEM_ERROR)
                return make_error(env, "enomem");
            return make_error(env, "einval");
        }
    } while (1);

    if (!enif_realloc_binary(&result, result.size - d_stream->avail_out)) {
        return make_error(env, "enomem");
    }
    return make_result(env, &result);
}

static ErlNifFunc nif_funcs[] =
        {
                {"new",        0, new_nif},
                {"new",        3, new_with_params_nif},
                {"compress",   2, compress_nif},
                {"decompress", 2, decompress_nif},
        };

ERL_NIF_INIT(ezlib, nif_funcs, load, NULL, upgrade, NULL)
