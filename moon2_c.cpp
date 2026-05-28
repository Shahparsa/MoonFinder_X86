#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <chrono>

namespace fs = std::filesystem;

int clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), s.begin());
}

bool ends_with_case_insensitive(const std::string& s, const std::string& suffix) {
    if (suffix.size() > s.size()) return false;
    for (size_t i = 0; i < suffix.size(); ++i) {
        char a = s[s.size() - suffix.size() + i];
        char b = suffix[i];
        if (std::tolower((unsigned char)a) != std::tolower((unsigned char)b)) return false;
    }
    return true;
}

static std::string stem_no_ext(const std::string& filename) {
    auto dot = filename.find_last_of('.');
    if (dot == std::string::npos) return filename;
    return filename.substr(0, dot);
}

static std::vector<fs::path> list_images(const fs::path& dir, const std::string& prefix, const std::string& ext = ".jpg"){
    std::vector<fs::path> files;
    if (!fs::exists(dir) || !fs::is_directory(dir)) return files;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        auto p = entry.path();
        std::string name = p.filename().string();
        if (!starts_with(name, prefix)) continue;
        if (!ends_with_case_insensitive(name, ext)) continue;
        files.push_back(p);
    }

    std::sort(files.begin(), files.end());
    return files;
}

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

// 3x3 blur
void blur3x3(const uint8_t* in, uint8_t* out, int width, int height){
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            int sum = 0;

            sum += in[(y-1)*width + (x-1)] * 1;
            sum += in[(y-1)*width + x] * 2;
            sum += in[(y-1)*width + (x+1)] * 1;
            sum += in[y*width + (x-1)] * 2;
            sum += in[y*width + x] * 4;
            sum += in[y*width + (x+1)] * 2;
            sum += in[(y+1)*width + (x-1)] * 1;
            sum += in[(y+1)*width + x] * 2;
            sum += in[(y+1)*width + (x+1)] * 1;

            out[y*width + x] = (uint8_t)(sum >> 4);
        }
    }
}

// 3x3 Convolution (u8 -> s16)
void conv3x3_u8_s16(const uint8_t* in, int16_t* out, int width, int height, const int* kernel){
    for (int y = 1; y < height-1; y++) {
        for (int x = 1; x < width-1; x++) {
            int sum = 0;

            sum += in[(y-1)*width + (x-1)] * kernel[0];
            sum += in[(y-1)*width + x] * kernel[1];
            sum += in[(y-1)*width + (x+1)] * kernel[2];
            sum += in[y*width + (x-1)] * kernel[3];
            sum += in[y*width + x] * kernel[4];
            sum += in[y*width + (x+1)] * kernel[5];
            sum += in[(y+1)*width + (x-1)] * kernel[6];
            sum += in[(y+1)*width + x] * kernel[7];
            sum += in[(y+1)*width + (x+1)] * kernel[8];

            out[y*width + x] = (int16_t)clamp(sum, -32768, 32767);
        }
    }
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

// Detector: returns true if found
bool detectMoon(const std::string& in_path, const std::string& out_png){
    int width, height, channels;
    uint8_t* img = stbi_load(in_path.c_str(), &width, &height, &channels, 3);
    if (!img) {
        std::cerr << "Failed to load image: " << in_path << "\n";
        return false;
    }

    const size_t n = (size_t)width * (size_t)height;

    uint8_t* gray = (uint8_t*)std::malloc(n);
    uint8_t* blur = (uint8_t*)std::malloc(n);
    int16_t* gx = (int16_t*)std::malloc(n * sizeof(int16_t));
    int16_t* gy = (int16_t*)std::malloc(n * sizeof(int16_t));
    uint8_t* edge = (uint8_t*)std::malloc(n);

    if (!gray || !blur || !gx || !gy || !edge) {
        std::cerr << "Out of memory\n";
        std::free(gray); std::free(blur); std::free(gx); std::free(gy); std::free(edge);
        stbi_image_free(img);
        return false;
    }

    // RGB -> Gray
    for (int i = 0; i < width*height; i++) {
        int r = img[i*3+0];
        int g = img[i*3+1];
        int b = img[i*3+2];
        gray[i] = (uint8_t)clamp((int)(0.299*r + 0.587*g + 0.114*b), 0, 255);
    }

    // Blur
    std::memset(blur, 0, n);
    blur3x3(gray, blur, width, height);

    // Sobel
    std::memset(gx, 0, n * sizeof(int16_t));
    std::memset(gy, 0, n * sizeof(int16_t));
    conv3x3_u8_s16(blur, gx, width, height, Kx);
    conv3x3_u8_s16(blur, gy, width, height, Ky);

    // Mean brightness
    long long sumB = 0;
    for (int i = 0; i < width*height; i++) {
        sumB += blur[i];
    }
    int meanB = (int)(sumB / (long long)(width*height));

    int brightThreshold = meanB + 20;
    if (brightThreshold > 230) {
        brightThreshold = 230;
    }
    if (brightThreshold < 60) {
        brightThreshold = 60;
    }

    const int edgeThreshold = 180;

    // edge mask
    for (int i = 0; i < width*height; i++) {
        int ax = gx[i]; if (ax < 0) ax = -ax;
        int ay = gy[i]; if (ay < 0) ay = -ay;
        int mag = ax + ay;

        int isEdge   = (mag > edgeThreshold);
        int isBright = (blur[i] > brightThreshold);

        edge[i] = (isEdge && isBright) ? 1 : 0;
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

    // Hough vote
    for (int r = r_min; r <= r_max; r += 2) {
        int* acc = (int*)std::calloc((size_t)width*height, sizeof(int));
        if (!acc) {
            std::cerr << "Out of memory (acc)\n";
            break;
        }

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                // فقط نقاطی که لبه هستند بررسی می شود
                if (!edge[y*width + x]) continue;

                for (int t = 0; t < 360; t += 5) {
                    float rad = (float)t * 3.1415926f / 180.0f;
                    int cx = x - (int)lroundf((float)r * cosf(rad));
                    int cy = y - (int)lroundf((float)r * sinf(rad));
                    if ((unsigned)cx < (unsigned)width && (unsigned)cy < (unsigned)height) {
                        acc[cy*width + cx]++;
                    }
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

        std::free(acc);
    }
    
    // با چک کردن حالات مختلف مقدار 27 در نظر گرفته شده است و می توان مقادیر دیگری هم انتخاب کرد.
    const int acceptScore = 27;
    bool found = (bestScore > acceptScore);

    // اگر ماه پیدا شد و مسیر خروجی هم داده شده، PNG خروجی ذخیره کن
    if (found && !out_png.empty()) {
        draw_circle(img, width, height, bestX, bestY, bestR);
        if (!stbi_write_png(out_png.c_str(), width, height, 3, img, width*3)) {
            std::cerr << "Failed to write output png: " << out_png << "\n";
        }
    }

    std::free(gray);
    std::free(blur);
    std::free(gx);
    std::free(gy);
    std::free(edge);
    stbi_image_free(img);

    return found;
}

struct Metrics {
    long long TP = 0;
    long long TN = 0;
    long long FP = 0;
    long long FN = 0;
};

static void print_metrics(const Metrics& m, double elapsed_ms) {
    const long long total = m.TP + m.TN + m.FP + m.FN;

    auto safe_div = [](double a, double b) -> double { return (b == 0.0) ? 0.0 : (a / b); };

    const double accuracy    = safe_div(double(m.TP + m.TN), double(total));
    const double recall      = safe_div(double(m.TP), double(m.TP + m.FN)); // TPR
    const double precision   = safe_div(double(m.TP), double(m.TP + m.FP));
    const double specificity = safe_div(double(m.TN), double(m.TN + m.FP)); // TNR
    const double f1          = (precision + recall == 0.0) ? 0.0 : 2.0 * precision * recall / (precision + recall);

    std::cout << "\n====== RESULTS ======\n";
    std::cout << "TP (moon -> moon)       : " << m.TP << "\n";
    std::cout << "TN (no_moon -> no_moon) : " << m.TN << "\n";
    std::cout << "FP (no_moon -> moon)    : " << m.FP << "\n";
    std::cout << "FN (moon -> no_moon)    : " << m.FN << "\n";
    std::cout << "Total                   : " << total << "\n";

    std::cout.setf(std::ios::fixed);
    std::cout.precision(2);
    std::cout << "Accuracy                : " << (accuracy * 100.0) << "%\n";
    std::cout << "Recall (TPR)            : " << (recall * 100.0) << "%\n";
    std::cout << "Precision               : " << (precision * 100.0) << "%\n";
    std::cout << "Specificity (TNR)       : " << (specificity * 100.0) << "%\n";
    std::cout << "F1-score                : " << (f1 * 100.0) << "%\n";
    std::cout << "Total time              : " << elapsed_ms << " ms\n";
}

int main(int argc, char** argv){
    auto t0 = std::chrono::steady_clock::now();

    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <moon_dir> <no_moon_dir>\n";
        return 0;
    }

    fs::path moon_dir = argv[1];
    fs::path no_moon_dir = argv[2];

    auto moon_files = list_images(moon_dir, "moon", ".jpg");
    auto no_moon_files = list_images(no_moon_dir, "no_moon", ".jpg");

    if (moon_files.empty() && no_moon_files.empty()) {
        std::cout << "No matching images found.\n";
        return 0;
    }

    // فولدر output را بساز (کنار اجرای برنامه)
    fs::path out_dir = "output";
    std::error_code ec;
    fs::create_directories(out_dir, ec);
    if (ec) {
        std::cerr << "Warning: could not create output dir: " << out_dir.string()
                  << " (" << ec.message() << ")\n";
    }

    Metrics M{};
    long long idx = 1;

    // moon: اگر predMoon==true خروجی ذخیره می‌شود
    for (const auto& p : moon_files) {
        std::string out_png = (out_dir / (stem_no_ext(p.filename().string()) + ".png")).string();
        bool predMoon = detectMoon(p.string(), out_png);

        if (predMoon) M.TP++;
        else M.FN++;

        std::cout << "[" << idx++ << "] " << p.string()
                  << " | actual=moon pred=" << (predMoon ? "moon" : "no_moon")
                  << (predMoon ? " | saved=output" : "")
                  << "\n";
    }

    // no_moon: خروجی ذخیره نمی‌شود
    for (const auto& p : no_moon_files) {
        bool predMoon = detectMoon(p.string(), /*out_png=*/"");
        if (!predMoon) M.TN++;
        else M.FP++;

        std::cout << "[" << idx++ << "] " << p.string()
                  << " | actual=no_moon pred=" << (predMoon ? "moon" : "no_moon") << "\n";
    }

    auto t1 = std::chrono::steady_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    print_metrics(M, elapsed_ms);
    return 0;
}