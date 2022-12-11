/* 
 * trans.c - Matrix transpose B = A^T
 *
 * Each transpose function must have a prototype of the form:
 * void trans(int M, int N, int A[N][M], int B[M][N]);
 *
 * A transpose function is evaluated by counting the number of misses
 * on a 1KB direct mapped cache with a block size of 32 bytes.
 */
#include <stdio.h>
#include "cachelab.h"

#define max(a,b)  (((a) > (b)) ? (a) : (b))
#define min(a,b)  (((a) < (b)) ? (a) : (b))
static const int BLOCK_SIZE = 8;

int is_transpose(int M, int N, int A[N][M], int B[M][N]);
void trans_32_32(int M, int N, int A[N][M], int B[M][N]);
void trans_64_64(int M, int N, int A[N][M], int B[M][N]);
void trans_61_67(int M, int N, int A[N][M], int B[M][N]);


/* 
 * transpose_submit - This is the solution transpose function that you
 *     will be graded on for Part B of the assignment. Do not change
 *     the description string "Transpose submission", as the driver
 *     searches for that string to identify the transpose function to
 *     be graded. 
 */
char transpose_submit_desc[] = "Transpose submission";
void transpose_submit(int M, int N, int A[N][M], int B[M][N])
{
    switch (M) {
        case 32:
            trans_32_32(M, N, A, B);
            break;
        case 64:
            trans_64_64(M, N, A, B);
            break;
        case 61:
            trans_61_67(M, N, A, B);
        default:
            break;
    }
}

/* 
 * You can define additional transpose functions below. We've defined
 * a simple one below to help you get started. 
 */

/* 
 * trans - A simple baseline transpose function, not optimized for the cache.
 */
char trans_desc[] = "Simple row-wise scan transpose";
void trans(int M, int N, int A[N][M], int B[M][N])
{
    int i, j, tmp;

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; j++) {
            tmp = A[i][j];
            B[j][i] = tmp;
        }
    }    

}

void trans_32_32(int M, int N, int A[N][M], int B[M][N])
{
    int i, j, k, l; 

    int is_diagonal, tmp = 0;
    for (i = 0; i < N; i += BLOCK_SIZE) {
        for (j = 0; j < M; j += BLOCK_SIZE) {
            for (k = i; k < i + BLOCK_SIZE; ++k) {
                for (l = j; l < j + BLOCK_SIZE; ++l) {
                    if (k != l) {
                        B[l][k] = A[k][l];
                        continue;
                    }
                    is_diagonal = 1;
                    tmp = A[k][l];
                }
                if (is_diagonal) {
                    B[k][k] = tmp;
                    is_diagonal = 0;
                }
            }
        }
    }
}

void trans_64_64(int M, int N, int A[N][M], int B[M][N])
{
    int i, j, k, l;
    int is_diagonal, tmp = 0;
    int tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    for (i = 0; i < M; i += BLOCK_SIZE) {
        for (j = 0; j < N; j += BLOCK_SIZE) {

            /* left top */
            for (k = i; k < i + BLOCK_SIZE / 2; ++k) { 
                for (l = j; l < j + BLOCK_SIZE / 2; ++l) {
                    if (k != l) {
                        B[l][k] = A[k][l];
                        continue;
                    }
                    is_diagonal = 1;
                    tmp = A[k][l];
                }
                if (is_diagonal) {
                    B[k][k] = tmp;
                    is_diagonal = 0;
                }
            }
            
            /* right top A -> B */
            for (k = i; k < i + BLOCK_SIZE / 2; ++k) {
                tmp4 = A[k][j + BLOCK_SIZE / 2];
                tmp5 = A[k][j + BLOCK_SIZE / 2 + 1];
                tmp6 = A[k][j + BLOCK_SIZE / 2 + 2];
                tmp7 = A[k][j + BLOCK_SIZE / 2 + 3];

                B[j][k + BLOCK_SIZE / 2] = tmp4;
                B[j + 1][k + BLOCK_SIZE / 2] = tmp5;
                B[j + 2][k + BLOCK_SIZE / 2] = tmp6;
                B[j + 3][k + BLOCK_SIZE / 2] = tmp7;
            }

            /* left bottom */
            for (l = j; l < j + BLOCK_SIZE / 2; ++l) { 
                // B right top row
                tmp0 = B[l][i + BLOCK_SIZE / 2];
                tmp1 = B[l][i + BLOCK_SIZE / 2 + 1];
                tmp2 = B[l][i + BLOCK_SIZE / 2 + 2];
                tmp3 = B[l][i + BLOCK_SIZE / 2 + 3];

                // A left bottom col
                tmp4 = A[i + BLOCK_SIZE / 2][l];
                tmp5 = A[i + BLOCK_SIZE / 2 + 1][l];
                tmp6 = A[i + BLOCK_SIZE / 2 + 2][l];
                tmp7 = A[i + BLOCK_SIZE / 2 + 3][l];

                // A left bottom col -> B right top row
                B[l][i + BLOCK_SIZE / 2] = tmp4;
                B[l][i + BLOCK_SIZE / 2 + 1] = tmp5;
                B[l][i + BLOCK_SIZE / 2 + 2] = tmp6;
                B[l][i + BLOCK_SIZE / 2 + 3] = tmp7;

                // B right top row -> B left bottom row
                B[l + BLOCK_SIZE / 2][i] = tmp0;
                B[l + BLOCK_SIZE / 2][i + 1] = tmp1;
                B[l + BLOCK_SIZE / 2][i + 2] = tmp2;
                B[l + BLOCK_SIZE / 2][i + 3] = tmp3;
            }

            /* right bottom */
            for (k = i + BLOCK_SIZE / 2; k < i + BLOCK_SIZE; ++k) { 
                for (int l = j + BLOCK_SIZE / 2; l < j + BLOCK_SIZE; ++l) {
                    if (k != l) {
                        B[l][k] = A[k][l];
                        continue;
                    }
                    is_diagonal = 1;
                    tmp = A[k][l];
                }
                if (is_diagonal) {
                    B[k][k] = tmp;
                    is_diagonal = 0;
                }
            }
        }
    }
}

void trans_61_67(int M, int N, int A[N][M], int B[M][N])
{
    int i, j, k, l; 
    for (i = 0; i < N; i += BLOCK_SIZE) {
		for (j = 0; j < M; j += BLOCK_SIZE) {
			for (k = j; k < min(j + BLOCK_SIZE, M); ++k) {
				for (l = i; l < min(i + BLOCK_SIZE, N); ++l) {
					B[k][l] = A[l][k];
				}
			}
		}
	}
}

/*
 * registerFunctions - This function registers your transpose
 *     functions with the driver.  At runtime, the driver will
 *     evaluate each of the registered functions and summarize their
 *     performance. This is a handy way to experiment with different
 *     transpose strategies.
 */
void registerFunctions()
{
    /* Register your solution function */
    registerTransFunction(transpose_submit, transpose_submit_desc);

    /* Register any additional transpose functions */
    registerTransFunction(trans, trans_desc);
}

/* 
 * is_transpose - This helper function checks if B is the transpose of
 *     A. You can check the correctness of your transpose by calling
 *     it before returning from the transpose function.
 */
int is_transpose(int M, int N, int A[N][M], int B[M][N])
{
    int i, j;

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; ++j) {
            if (A[i][j] != B[j][i]) {
                return 0;
            }
        }
    }
    return 1;
}