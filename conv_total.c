#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

extern void conv3x3(const uint8_t* in, uint8_t* out, int width, int height, const int* k9);

uint8_t clamp(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

void conv3x3_c_rgb(const uint8_t* input, uint8_t* out, int width, int height, const int* kernel) {
    memcpy(out, input, (size_t)width * (size_t)height * 3);

    for (int y = 1; y < height - 1; y++) {
        for (int x = 1; x < width - 1; x++) {
            int idx = (y * width + x) * 3;

            for (int c = 0; c < 3; c++) {
                int sum = 0;

                sum += kernel[0] * input[((y - 1) * width + (x - 1)) * 3 + c];
                sum += kernel[1] * input[((y - 1) * width + x) * 3 + c];
                sum += kernel[2] * input[((y - 1) * width + (x + 1)) * 3 + c];

                sum += kernel[3] * input[(y * width + (x - 1)) * 3 + c];
                sum += kernel[4] * input[(y * width + x) * 3 + c];
                sum += kernel[5] * input[(y * width + (x + 1)) * 3 + c];

                sum += kernel[6] * input[((y + 1) * width + (x - 1)) * 3 + c];
                sum += kernel[7] * input[((y + 1) * width + x) * 3 + c];
                sum += kernel[8] * input[((y + 1) * width + (x + 1)) * 3 + c];

                out[idx + c] = clamp(sum);
            }
        }
    }
}

void rgb_to_planar(const uint8_t* rgb, uint8_t* R, uint8_t* G, uint8_t* B, int width, int height) {
    int n = width * height;
    for (int i = 0; i < n; i++) {
        R[i] = rgb[i*3 + 0];
        G[i] = rgb[i*3 + 1];
        B[i] = rgb[i*3 + 2];
    }
}

void planar_to_rgb(uint8_t* rgb, const uint8_t* R, const uint8_t* G, const uint8_t* B, int width, int height) {
    int n = width * height;
    for (int i = 0; i < n; i++) {
        rgb[i*3 + 0] = R[i];
        rgb[i*3 + 1] = G[i];
        rgb[i*3 + 2] = B[i];
    }
}

double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

int main(int argc, char** argv) {
    if (argc != 13) {
        printf("Wrong input format\n");
        printf("Correct format:\n");
        printf("  %s <input.png> <out_c.png> <out_asm.png> k00 k01 k02 k10 k11 k12 k20 k21 k22\n", argv[0]);
        return 1;
    }

    const char* input   = argv[1];
    const char* out_c   = argv[2];
    const char* out_asm = argv[3];

    int kernel[9];
    for (int i = 0; i < 9; i++){
        kernel[i] = atoi(argv[4 + i]);
    }

    int width, height, channel;
    uint8_t* image = stbi_load(input, &width, &height, &channel, 3);
    if (!image) {
        printf("Can't load image\n");
        return 1;
    }

    // if too small for 3x3, just copy input to output
    if (width < 3 || height < 3) {
        printf("Image too small for 3x3 convolution\n");
        stbi_image_free(image);
        return 1;
    }

    size_t n_pix = (size_t)width * (size_t)height;
    size_t n_bytes = n_pix * 3;

    // C
    uint8_t* outC = (uint8_t*)malloc(n_bytes);

    // ASM
    uint8_t *R1  = (uint8_t*)malloc(n_pix);
    uint8_t *G1  = (uint8_t*)malloc(n_pix);
    uint8_t *B1  = (uint8_t*)malloc(n_pix);

    uint8_t *R2 = (uint8_t*)malloc(n_pix);
    uint8_t *G2 = (uint8_t*)malloc(n_pix);
    uint8_t *B2 = (uint8_t*)malloc(n_pix);

    uint8_t* outASM = (uint8_t*)malloc(n_bytes);

    if (!outC || !R1 || !G1 || !B1 || !R2 || !G2 || !B2 || !outASM) {
        printf("Memory error\n");
        stbi_image_free(image);
        free(outC);
        free(R1); free(G1); free(B1);
        free(R2); free(G2); free(B2);
        free(outASM);
        return 1;
    }

    // number of runs for averaging time
    int runs = 20;

    // C
    double c_time = 0.0;
    for (int i = 0; i < runs; i++) {
        double t1 = now_ms();
        conv3x3_c_rgb(image, outC, width, height, kernel);
        double t2 = now_ms();
        c_time += t2 - t1;
    }
    double ms_c = c_time / runs;

    // ASM
    rgb_to_planar(image, R1, G1, B1, width, height);
    double asm_time = 0.0;
    for (int i = 0; i < runs; i++) {
        double t3 = now_ms();
        conv3x3(R1, R2, width, height, kernel);
        conv3x3(G1, G2, width, height, kernel);
        conv3x3(B1, B2, width, height, kernel);
        double t4 = now_ms();
        asm_time += t4 - t3;
    }
    double ms_asm = asm_time / runs;
    planar_to_rgb(outASM, R2, G2, B2, width, height);

    // Write
    if (!stbi_write_png(out_c, width, height, 3, outC, width * 3)) {
        printf("Error: cannot write %s\n", out_c);
    }
    if (!stbi_write_png(out_asm, width, height, 3, outASM, width * 3)) {
        printf("Error: cannot write %s\n", out_asm);
    }

    printf("Results:\n");
    printf("C-only time: %.3f ms\n", ms_c);
    printf("C+ASM time: %.3f ms\n", ms_asm);
    if (ms_asm > 0.0) printf("Speedup (C/ASM): %.2fx\n", ms_c / ms_asm);

    stbi_image_free(image);
    free(outC);
    free(R1); free(G1); free(B1);
    free(R2); free(G2); free(B2);
    free(outASM);

    return 0;
}