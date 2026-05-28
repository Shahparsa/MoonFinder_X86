#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <time.h>

int clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// ASM(Blur + Sobel)
void blur3x3(const uint8_t* in, uint8_t* out, int width, int height);
void conv3x3_u8_s16_asm(const uint8_t* in, int16_t* out, int width, int height, const int* kernel);

void draw_circle(uint8_t* img, int width, int height, int cx, int cy, int r){
    int thickness = 1;

    for (int t = 0; t < 360; t++) {
        float rad = (float)t * 3.1415926f / 180.0f;
        int x0 = cx + (int)lroundf(r * cosf(rad));
        int y0 = cy + (int)lroundf(r * sinf(rad));

        for (int dy = -thickness; dy <= thickness; dy++) {
            for (int dx = -thickness; dx <= thickness; dx++) {
                int x = x0 + dx;
                int y = y0 + dy;
                if ((unsigned)x < (unsigned)width && (unsigned)y < (unsigned)height) {
                    int idx = (y*width + x) * 3;
                    img[idx+0] = 255;
                    img[idx+1] = 0;
                    img[idx+2] = 0;
                }
            }
        }
    }
}

double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

// Kernels Sobels
int Kx[9] = {
    -1,0,1,
    -2,0,2,
    -1,0,1
};

int Ky[9] = {
    -1,-2,-1,
    0,0,0,
    1,2,1
};

int main(int argc, char** argv){
    double t0 = now_ms();
    if (argc < 3) {
        printf("Usage: %s input.jpg output.png\n", argv[0]);
        return 0;
    }

    int width, height, channel;
    uint8_t* img = stbi_load(argv[1], &width, &height, &channel, 3);
    if (!img) {
        printf("Failed to load image\n");
        return 0;
    }

    printf("Image loaded: %dx%d\n", width, height);

    uint8_t* gray = (uint8_t*)malloc((size_t)width*height);
    uint8_t* blur = (uint8_t*)malloc((size_t)width*height);
    int16_t* gx = (int16_t*)malloc((size_t)width*height*sizeof(int16_t));
    int16_t* gy = (int16_t*)malloc((size_t)width*height*sizeof(int16_t));
    uint8_t* edge = (uint8_t*)malloc((size_t)width*height);

    if (!gray || !blur || !gx || !gy || !edge) {
        printf("Out of memory\n");
        free(gray); free(blur); free(gx); free(gy); free(edge);
        stbi_image_free(img);
        return 0;
    }

    // RGB -> Gray
    for (int i = 0; i < width*height; i++) {
        int r = img[i*3+0];
        int g = img[i*3+1];
        int b = img[i*3+2];
        gray[i] = (uint8_t)clamp((int)(0.299*r + 0.587*g + 0.114*b), 0, 255);
    }

    // blur
    memset(blur, 0, (size_t)width*height);
    blur3x3(gray, blur, width, height);

    // sobel
    memset(gx, 0, (size_t)width*height*sizeof(int16_t));
    memset(gy, 0, (size_t)width*height*sizeof(int16_t));
    conv3x3_u8_s16_asm(blur, gx, width, height, Kx);
    conv3x3_u8_s16_asm(blur, gy, width, height, Ky);

    // Mean brightness
    long long sumB = 0;
    for (int i = 0; i < width*height; i++) sumB += blur[i];
    int meanB = (int)(sumB / (width*height));

    int brightThreshold = meanB + 20;
    if (brightThreshold > 230){
        brightThreshold = 230;
    }
    if (brightThreshold < 60){  
        brightThreshold = 60;
    }

    int edgeThreshold = 180;

    // Build edge mask: strong edge AND bright area
    for (int i = 0; i < width*height; i++) {
        int ax = gx[i]; 
        if (ax < 0){ 
            ax = -ax;
        }
        int ay = gy[i];
        if (ay < 0){ 
            ay = -ay;
        }
        int mag = ax + ay;

        int isEdge = (mag > edgeThreshold);
        int isBright = (blur[i] > brightThreshold);

        if (mag > edgeThreshold && blur[i] > brightThreshold){
            edge[i] = 1;
        }else{
            edge[i] = 0;
        }    
    }

    int bestScore = 0;
    int bestX = 0, bestY = 0, bestR = 0;

    int m;
    if (width < height){
        m = width;
    }else{
        m = height;
    }
    
    // مقادیر گفته شده حدودی می باشد و می توان عدد ها را تغییر داد 
    int r_min = m / 12;
    int r_max = m / 2;
    
    if (r_min < 8){
        r_min = 8;
    }
    if (r_max < 20){
        r_max = 20;
    }

    printf("Searching radius: r_min=%d r_max=%d\n", r_min, r_max);
    printf("Brightness mean=%d => brightThreshold=%d, edgeThreshold=%d\n",meanB, brightThreshold, edgeThreshold);
    
    // Hough vote
    for (int r = r_min; r <= r_max; r += 2) {
        int* acc = (int*)calloc((size_t)width*height, sizeof(int));
        if (!acc) {
            printf("Out of memory (acc)\n");
            break;
        }

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                // فقط نقاطی که لبه هستند بررسی می شود
                if (!edge[y*width + x]) continue;

                for (int t = 0; t < 360; t += 5) {
                    float rad = (float)t * 3.1415926f / 180.0f;
                    int cx = x - (int)lroundf(r * cosf(rad));
                    int cy = y - (int)lroundf(r * sinf(rad));
                    if ((unsigned)cx < (unsigned)width && (unsigned)cy < (unsigned)height)
                        acc[cy*width + cx]++;
                }
            }
        }

        for (int i = 0; i < width*height; i++) {
            if (acc[i] > bestScore) {
                bestScore = acc[i];
                bestY = i / width;
                bestX = i % width;
                bestR = r;
            }
        }

        free(acc);
    }

    // با چک کردن حالات مختلف مقدار 27 در نظر گرفته شده است و می توان مقادیر دیگری هم انتخاب کرد.
    int acceptScore = 27;

    if (bestScore > acceptScore) {
        printf("Moon found at (%d,%d) radius=%d score=%d\n",
               bestX, bestY, bestR, bestScore);
        draw_circle(img, width, height, bestX, bestY, bestR);
    } else {
        printf("Moon not found (bestScore=%d)\n", bestScore);
    }

    stbi_write_png(argv[2], width, height, 3, img, width*3);

    free(gray);
    free(blur);
    free(gx);
    free(gy);
    free(edge);
    stbi_image_free(img);

    double t1 = now_ms();
    printf("C+ASM time: %.3f ms\n", t1 - t0);

    return 0;
}