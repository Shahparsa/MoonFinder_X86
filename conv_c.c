#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

int kernel[3][3];

uint8_t clamp(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

void conv3x3(const uint8_t* input, uint8_t* out, int w, int h) {
    // copy input -> out (keep borders)
    memcpy(out, input, (size_t)w * (size_t)h * 3);

    for (int y = 1; y < h - 1; y++) {
        for (int x = 1; x < w - 1; x++) {
            int idx = (y * w + x) * 3;

            for (int c = 0; c < 3; c++) {
                int sum = 0;

                sum += kernel[0][0] * input[((y - 1) * w + (x - 1)) * 3 + c];
                sum += kernel[0][1] * input[((y - 1) * w + x) * 3 + c];
                sum += kernel[0][2] * input[((y - 1) * w + (x + 1)) * 3 + c];

                sum += kernel[1][0] * input[(y * w + (x - 1)) * 3 + c];
                sum += kernel[1][1] * input[(y * w + x) * 3 + c];
                sum += kernel[1][2] * input[(y * w + (x + 1)) * 3 + c];

                sum += kernel[2][0] * input[((y + 1) * w + (x - 1)) * 3 + c];
                sum += kernel[2][1] * input[((y + 1) * w + x) * 3 + c];
                sum += kernel[2][2] * input[((y + 1) * w + (x + 1)) * 3 + c];

                out[idx + c] = clamp(sum);
            }
        }
    }
}

double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

int main(int argc, char** argv) {
    double t0 = now_ms();
    if (argc != 12) {
        printf("Wrong input format\n");
        printf("Correct format: %s <input.png> <output.png> k00 k01 k02 k10 k11 k12 k20 k21 k22\n", argv[0]);
        return 1;
    }

    const char* input  = argv[1];
    const char* output = argv[2];

    int s = 3;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            kernel[i][j] = atoi(argv[s++]);
        }
    }

    int width, height, channel;
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

    size_t nbytes = (size_t)width * (size_t)height * 3;
    uint8_t* out = (uint8_t*)malloc(nbytes);
    if (!out) {
        printf("Memory error\n");
        stbi_image_free(image);
        return 1;
    }

    double t1 = now_ms();
    conv3x3(image, out, width, height);
    double t2 = now_ms();

    if (!stbi_write_png(output, width, height, 3, out, width * 3)) {
        printf("Error: cannot write %s\n", output);
        stbi_image_free(image);
        free(out);
        return 1;
    }

    stbi_image_free(image);
    free(out);
    double t4 = now_ms();
    
    printf("C-only processing: %.3f ms\n", (t2 - t1));
    printf("C-only total processing: %.3f ms\n", (t4 - t0));
    printf("Done! Saved %s (%dx%d)\n", output, width, height);
    return 0;
}