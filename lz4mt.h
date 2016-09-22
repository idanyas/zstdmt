
/**
 * Copyright © 2016 Tino Reichardt
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 *
 * You can contact the author at:
 * - zstdmt source repository: https://github.com/mcmilk/zstdmt
 */

/* ***************************************
 * Defines
 ****************************************/

#ifndef LZ4MT_H
#define LZ4MT_H

#if defined (__cplusplus)
extern "C" {
#endif

/* current maximum the library will accept */
#define LZ4MT_THREAD_MAX 128

/* **************************************
 * Structures
 ****************************************/

typedef struct {
	void *buf;		/* ptr to data */
	size_t size;		/* length of buf */
} LZ4MT_Buffer;

/**
 * reading and writing functions
 * - you can use stdio functions or plain read/write
 * - just write some wrapper on your own
 * - a sample is given in 7-Zip ZS
 */
typedef int (fn_read) (void *args, LZ4MT_Buffer * in);
typedef int (fn_write) (void *args, LZ4MT_Buffer * out);

typedef struct {
	fn_read *fn_read;
	void *arg_read;
	fn_write *fn_write;
	void *arg_write;
} LZ4MT_RdWr_t;

/* **************************************
 * Compression
 ****************************************/

typedef struct LZ4MT_CCtx_s LZ4MT_CCtx;

/**
 * 1) allocate new cctx
 * - return cctx or zero on error
 *
 * @level   - 1 .. 9
 * @threads - 1 .. LZ4MT_THREAD_MAX
 * @inputsize - if zero, becomes some optimal value for the level
 *            - if nonzero, the given value is taken
 */
LZ4MT_CCtx *LZ4MT_createCCtx(int threads, int level, int inputsize,
			     int blockSizeID);

/**
 * 2) threaded compression
 * - return -1 on error
 */
int LZ4MT_CompressCCtx(LZ4MT_CCtx * ctx, LZ4MT_RdWr_t * rdwr);

/**
 * 3) get some statistic
 */
size_t LZ4MT_GetFramesCCtx(LZ4MT_CCtx * ctx);
size_t LZ4MT_GetInsizeCCtx(LZ4MT_CCtx * ctx);
size_t LZ4MT_GetOutsizeCCtx(LZ4MT_CCtx * ctx);

/**
 * 4) free cctx
 * - no special return value
 */
void LZ4MT_freeCCtx(LZ4MT_CCtx * ctx);

/* **************************************
 * Decompression - TODO, but it's easy...
 ****************************************/

typedef struct LZ4MT_DCtx_s LZ4MT_DCtx;

/**
 * 1) allocate new cctx
 * - return cctx or zero on error
 *
 * @level   - 1 .. 22
 * @threads - 1 .. LZ4MT_THREAD_MAX
 * @srclen  - the max size of src for LZ4MT_CompressCCtx()
 * @dstlen  - the min size of dst
 */
LZ4MT_DCtx *LZ4MT_createDCtx(int threads);

/**
 * 2) threaded compression
 * - return -1 on error
 */
int LZ4MT_DecompressDCtx(LZ4MT_DCtx * ctx, LZ4MT_RdWr_t * rdwr);

/**
 * 3) get some statistic
 */
size_t LZ4MT_GetFramesDCtx(LZ4MT_DCtx * ctx);
size_t LZ4MT_GetInsizeDCtx(LZ4MT_DCtx * ctx);
size_t LZ4MT_GetOutsizeDCtx(LZ4MT_DCtx * ctx);

/**
 * 4) free cctx
 * - no special return value
 */
void LZ4MT_freeDCtx(LZ4MT_DCtx * ctx);

#if defined (__cplusplus)
}
#endif
#endif				/* LZ4MT_H */