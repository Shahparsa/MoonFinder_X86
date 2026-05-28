
section .data
align 16
w1:   dw 1,1,1,1,1,1,1,1
w2:   dw 2,2,2,2,2,2,2,2
w4:   dw 4,4,4,4,4,4,4,4
w8:   dw 8,8,8,8,8,8,8,8

section .text
    global blur3x3

%macro MULL 3                           ; تعریف ماکرو با 3 آرگومان
    movdqu xmm12, [%1 + rbx + %2]       ; بارگذاری 16 پیکسل (بایت) از تصویر ورودی به رجیستر xmm12

    movaps xmm13, xmm12                 ; کپی کردن مقدار در رجیستر xmm12 در رجیستر xmm13 (2 بایت 2 بایت کپی میکند)
    
    punpcklbw xmm13, xmm9               ; قرار دادن بایت‌های پایین رجیستر xmm13 در کلمات 16 بیتی (u8 -> u16) برای 8 پیکسل اول (0..7)
    pmullw    xmm13, %3                 ; ضرب در مقدار kernel مربوطه (word)
    paddw     xmm10, xmm13              ; اضافه کردن نتیجه به جمع پایین (low-sum) در رجیستر xmm10

    punpckhbw xmm12, xmm9               ; قرار دادن بایت‌های بالا رجیستر xmm12 در کلمات 16 بیتی (u8 -> u16) برای 8 پیکسل دوم (8..15)
    pmullw    xmm12, %3                 ; ضرب در مقدار kernel مربوطه (word)
    paddw     xmm11, xmm12              ; اضافه کردن نتیجه به جمع بالا (high-sum) در رجیستر xmm11
%endmacro                               ; پایان تعریف ماکرو

%macro MULL1 2                          ; تعریف ماکرو با 2 آرگومان
    movzx ecx, byte [%1 + rbx + %2]     ; بارگذاری یک پیکسل (بایت) از تصویر ورودی و تبدیل آن به مقدار بدون علامت 32 بیتی در رجیستر ecx
    add   r14d, ecx                     ; اضافه کردن مقدار پیکسل به جمع کل (acc) در رجیستر r14d
%endmacro                               ; پایان تعریف ماکرو

%macro MULL2 2                          ; تعریف ماکرو با 2 آرگومان
    movzx ecx, byte [%1 + rbx + %2]     ; بارگذاری یک پیکسل (بایت) از تصویر ورودی و تبدیل آن به مقدار بدون علامت 32 بیتی در رجیستر ecx
    shl ecx, 1                          ; ضرب مقدار پیکسل در 2 و ذخیره نتیجه در ecx
    add   r14d, ecx                     ; اضافه کردن نتیجه به جمع کل (acc) در رجیستر r14d
%endmacro                               ; پایان تعریف ماکرو

%macro MULL4 2                          ; تعریف ماکرو با 2 آرگومان
    movzx ecx, byte [%1 + rbx + %2]     ; بارگذاری یک پیکسل (بایت) از تصویر ورودی و تبدیل آن به مقدار بدون علامت 32 بیتی در رجیستر ecx
    shl ecx, 2                          ; ضرب مقدار پیکسل در 4 و ذخیره نتیجه در ecx
    add   r14d, ecx                     ; اضافه کردن نتیجه به جمع کل (acc) در رجیستر r14d
%endmacro                               ; پایان تعریف ماکرو

; rdi = input image pointer (u8)
; rsi = output image pointer (u8)
; rdx = width
; rcx = height
; Gaussian 3x3 kernel (sum=16):
;  1 2 1
;  2 4 2
;  1 2 1
;
blur3x3:
    push rbx                ; ذخیره رجسیتر rbx در استک
    push rbp                ; ذخیره رجسیتر rbp در استک
    push r12                ; ذخیره رجسیتر r12 در استک
    push r13                ; ذخیره رجسیتر r13 در استک
    push r14                ; ذخیره رجسیتر r14 در استک
    push r15                ; ذخیره رجسیتر r15 در استک
    
    mov  r9d, ecx           ; ذخیره ارتفاع تصویر در رجیستر r9d

    movdqa xmm0, [rel w1]     ; قرار دادن مقادیر 1 در رجیستر xmm0
    movdqa xmm1, [rel w2]     ; قرار دادن مقادیر 2 در رجیستر xmm1
    movdqa xmm2, [rel w4]     ; قرار دادن مقادیر 4 در رجیستر xmm2
    movdqa xmm3, [rel w8]     ; قرار دادن مقادیر 8 در رجیستر xmm3

    pxor xmm9, xmm9         ; صفر کردن رجیستر xmm9  

    mov  ebp, 1             ; شروع حلقه y از 1 (ردیف اول را نادیده می‌گیریم)

y_loop:
    mov  eax, r9d           ; بارگذاری ارتفاع تصویر در eax
    dec  eax                ; h-1
    cmp  ebp, eax           ; مقایسه y با h-1
    jge  done               ; اگر y >= h-1، به پایان برو

    ; row_offset_bytes_in  = y*w
    mov  eax, ebp           ; y 
    imul eax, edx           ; y*w
    movsxd rax, eax         ; تبدیل به 64 بیت

    ; cur/prev/next pointers
    lea  r10, [rdi + rax]     ; cur = in + off
    mov  r11, r10
    sub  r11, rdx             ; prev = cur - w
    lea  r12, [r10 + rdx]     ; next = cur + w
    lea  r13, [rsi + rax]     ; out_row = out + off

    mov  ebx, 1             ; x = 1 (ستون اول را نادیده می‌گیریم)

    ; if w-17 < 0 -> no 16-byte blocks
    mov  eax, edx           ; بارگذاری عرض تصویر در eax
    sub  eax, 17            ; w-17
    js   scalar_row         ; اگر w-17 < 0، به بخش محاسبات اسکالر برو
    mov  r14d, eax          ; last_vec_x = w-17

vec_loop:
    cmp  ebx, r14d          ; مقایسه x با last_vec_x
    jg   scalar_row         ; اگر x > last_vec_x، به بخش محاسبات اسکالر برو

    pxor xmm10, xmm10       ; صفر کردن رجیستر xmm10
    pxor xmm11, xmm11       ; صفر کردن رجیستر xmm11

    ; prev row: 1 2 1
    MULL r11, -1, xmm0     ; اعمال کرنل به پیکسل (x-1, y-1) با وزن کرنل در xmm0
    MULL r11,  0, xmm1     ; اعمال کرنل به پیکسل (x, y-1) با وزن کرنل در xmm1
    MULL r11,  1, xmm0     ; اعمال کرنل به پیکسل (x+1, y-1) با وزن کرنل در xmm0

    ; cur row: 2 4 2
    MULL r10, -1, xmm1     ; اعمال کرنل به پیکسل (x-1, y) با وزن کرنل در xmm1
    MULL r10,  0, xmm2     ; اعمال کرنل به پیکسل (x, y) با وزن کرنل در xmm2
    MULL r10,  1, xmm1     ; اعمال کرنل به پیکسل (x+1, y) با وزن کرنل در xmm1

    ; next row: 1 2 1
    MULL r12, -1, xmm0     ; اعمال کرنل به پیکسل (x-1, y+1) با وزن کرنل در xmm0
    MULL r12,  0, xmm1     ; اعمال کرنل به پیکسل (x, y+1) با وزن کرنل در xmm1
    MULL r12,  1, xmm0     ; اعمال کرنل به پیکسل (x+1, y+1) با وزن کرنل در xmm0

    ; normalize: (sum + 8) >> 4   (sum=16)
    paddw xmm10, xmm3       ; اضافه کردن 8 برای گرد کردن قبل از شیفت
    psrlw xmm10, 4          ; تقسیم بر 16 با شیفت راست
    paddw xmm11, xmm3       ; اضافه کردن 8 برای گرد کردن قبل از شیفت
    psrlw xmm11, 4          ; تقسیم بر 16 با شیفت راست

    ; low 8 words -> xmm10 (16 bytes)
    ; high 8 words -> xmm11 (16 bytes)
    packuswb xmm10, xmm11           ; 8 کلمه‌ی 16‌بیتی پایین از xmm10 و 8 کلمه‌ی 16‌بیتی از xmm11  به 8‌بیتی تبدیل شده و مجموعاً 16 بایت در xmm10 قرار می‌گیرند.
    movdqu [r13 + rbx], xmm10       ; مقدار حاصل محاسبه در رجیستر xmm10 در خروجی ذخیره می‌شود

    add  ebx, 16            ; پرش به 16 ستون بعدی (x += 16)
    jmp  vec_loop           ; تکرار حلقه برای بلوک بعدی 

scalar_row:
    mov  eax, edx           ; بارگذاری عرض تصویر در رجیستر eax
    dec  eax                ; w-1
    mov  r15d, eax          ; x_end = w-1

scalar_loop:
    cmp  ebx, r15d          ; مقایسه x با x_end
    jge  next_row           ; اگر x >= x_end، به سطر بعدی برو
                            ;اگر به انتهای سطر رسیدیم، به سطر بعد برو
    
    xor  r14d, r14d         ; صفر کردن رجیستر r14d برای محاسبات یک پیکسل (acc = 0)

    ; prev row: 1 2 1
    MULL1 r11, -1            ; ضرب پیکسل (x-1, y-1) در وزن کرنل مربوطه و اضافه کردن به acc
    MULL2 r11,  0            ; ضرب پیکسل (x, y-1) در وزن کرنل مربوطه و اضافه کردن به acc
    MULL1 r11,  1            ; ضرب پیکسل (x+1, y-1) در وزن کرنل مربوطه و اضافه کردن به acc

    ; cur row: 2 4 2
    MULL2 r10, -1            ; ضرب پیکسل (x-1, y) در وزن کرنل مربوطه و اضافه کردن به acc
    MULL4 r10,  0            ; ضرب پیکسل (x, y) در وزن کرنل مربوطه و اضافه کردن به acc
    MULL2 r10,  1            ; ضرب پیکسل (x+1, y) در وزن کرنل مربوطه و اضافه کردن به acc

    ; next row: 1 2 1
    MULL1 r12, -1            ; ضرب پیکسل (x-1, y+1) در وزن کرنل مربوطه و اضافه کردن به acc
    MULL2 r12,  0            ; ضرب پیکسل (x, y+1) در وزن کرنل مربوطه و اضافه کردن به acc
    MULL1 r12,  1            ; ضرب پیکسل (x+1, y+1) در وزن کرنل مربوطه و اضافه کردن به acc

    add  r14d, 8            ; اضافه کردن 8 برای گرد کردن قبل از شیفت
    shr  r14d, 4            ; تقسیم بر 16 با شیفت راست

    mov  byte [r13 + rbx], r14b     ; ذخیره نتیجه (با علامت) ۸ بیتی (بدون محدود کردن بین 0 تا 255) 

    inc  ebx                        ; رفتن به ستون بعدی (x += 1)                     
    jmp  scalar_loop                ; تکرار حلقه برای ستون بعدی

next_row:
    inc  ebp                ; رفتن به سطر بعدی (y += 1) 
    jmp  y_loop             ; تکرار حلقه برای سطر بعدی

done:
    pop  r15                ; بازیابی رجیستر r15 از استک
    pop  r14                ; بازیابی رجیستر r14 از استک
    pop  r13                ; بازیابی رجیستر r13 از استک
    pop  r12                ; بازیابی رجیستر r12 از استک
    pop  rbp                ; بازیابی رجیستر rbp از استک 
    pop  rbx                ; بازیابی رجیستر rbx از استک
    ret                     ; بازگشت 