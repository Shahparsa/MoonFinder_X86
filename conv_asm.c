#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <time.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

extern void conv3x3(const uint8_t* in, uint8_t* out, int width, int height, const int* kernel);

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
    double t0 = now_ms();
    // check input format
    if (argc != 12) {
        printf("Wrong input format\n");
        printf("Correct format: %s <input.png> <output.png> k00 k01 k02 k10 k11 k12 k20 k21 k22\n", argv[0]);
        return 1;
    }

    const char* input  = argv[1];
    const char* output = argv[2];

    // kernel
    int kernel[9];
    for (int i = 0; i < 9; i++) {
        // string to int
        kernel[i] = atoi(argv[3 + i]);
    }

    int width, height, channel;

    // load image
    uint8_t* image = stbi_load(input, &width, &height, &channel, 3);
    if (!image) {
        printf("Can't load image\n");
        return 1;
    }

    // if too small for 3x3, just copy input to output
    if (width < 3 || height < 3) {
        if (!stbi_write_png(output, width, height, 3, image, width * 3)) {
            printf("Error: cannot write %s\n", output);
            stbi_image_free(image);
            return 1;
        }
        printf("Image too small for 3x3 convolution. Saved original to %s (%dx%d)\n", output, width, height);
        stbi_image_free(image);
        return 0;
    }

    size_t n = (size_t)width * (size_t)height;

    uint8_t *R1  = (uint8_t*)malloc(n);
    uint8_t *G1  = (uint8_t*)malloc(n);
    uint8_t *B1  = (uint8_t*)malloc(n);

    uint8_t *R2 = (uint8_t*)malloc(n);
    uint8_t *G2 = (uint8_t*)malloc(n);
    uint8_t *B2 = (uint8_t*)malloc(n);

    uint8_t *out = (uint8_t*)malloc(n * 3);

    if (!R1 || !G1 || !B1 || !R2 || !G2 || !B2 || !out) {
        printf("Memory error\n");
        stbi_image_free(image);
        free(R1); free(G1); free(B1);
        free(R2); free(G2); free(B2);
        free(out);
        return 1;
    }

    // make rgb separate
    rgb_to_planar(image, R1, G1, B1, width, height);

    // init output with input (so untouched border stays valid)
    memcpy(R2, R1, n);
    memcpy(G2, G1, n);
    memcpy(B2, B1, n);

    double t1 = now_ms();

    conv3x3(R1, R2, width, height, kernel);
    conv3x3(G1, G2, width, height, kernel);
    conv3x3(B1, B2, width, height, kernel);

    double t2 = now_ms();

    // make photo again
    planar_to_rgb(out, R2, G2, B2, width, height);

    if (!stbi_write_png(output, width, height, 3, out, width * 3)) {
        printf("Error: cannot write %s\n", output);
        stbi_image_free(image);
        free(R1); free(G1); free(B1);
        free(R2); free(G2); free(B2);
        free(out);
        return 1;
    }

    // free things
    stbi_image_free(image);
    free(R1); free(G1); free(B1);
    free(R2); free(G2); free(B2);
    free(out);

    double t4 = now_ms();

    printf("C+ASM processing: %.3f ms\n", (t2 - t1));
    printf("C+ASM total processing: %.3f ms\n", (t4 - t0));
    printf("Done! Saved %s (%dx%d)\n", output, width, height);
    return 0;
}