#include <stdarg.h>
#include <stddef.h>
#include <limits.h>

int __wrap_vsscanf(const char* s, const char* f, va_list ap);
int __wrap_sscanf(const char* s, const char* f, ...);

static int scanf_isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

int __wrap_vsscanf(const char* s, const char* f, va_list ap) {
    int assigned = 0;
    const char* p = s;
    for(; *f; f++) {
        if(scanf_isspace((unsigned char)*f)) {
            while(scanf_isspace((unsigned char)*p)) {
                p++;
            }
            continue;
        }
        if(*f != '%') {
            if(*p != *f) {
                return assigned;
            }
            p++;
            continue;
        }
        f++;
        if(*f == '%') {
            if(*p != '%') {
                return assigned;
            }
            p++;
            continue;
        }
        int suppress = 0;
        if(*f == '*') {
            suppress = 1;
            f++;
        }
        int width = 0;
        int have_w = 0;
        while(*f >= '0' && *f <= '9') {
            have_w = 1;
            width = width * 10 + (*f - '0');
            f++;
        }
        int len = 0;
        if(*f == 'h') {
            f++;
            if(*f == 'h') {
                len = -2;
                f++;
            } else {
                len = -1;
            }
        } else if(*f == 'l') {
            f++;
            if(*f == 'l') {
                len = 2;
                f++;
            } else {
                len = 1;
            }
        }
        char conv = *f;
        if(conv == 's') {
            while(scanf_isspace((unsigned char)*p)) {
                p++;
            }
            if(!*p) {
                return assigned ? assigned : -1;
            }
            char* out = suppress ? NULL : va_arg(ap, char*);
            int n = 0;
            int w = have_w ? width : INT_MAX;
            while(*p && !scanf_isspace((unsigned char)*p) && n < w) {
                if(out) {
                    out[n] = *p;
                }
                n++;
                p++;
            }
            if(n == 0) {
                return assigned;
            }
            if(out) {
                out[n] = 0;
            }
            if(!suppress) {
                assigned++;
            }
            continue;
        }
        if(conv == 'd' || conv == 'i' || conv == 'u' || conv == 'x' || conv == 'X' ||
           conv == 'o' || conv == 'p') {
            while(scanf_isspace((unsigned char)*p)) {
                p++;
            }
            int base = (conv == 'x' || conv == 'X' || conv == 'p') ? 16 : (conv == 'o') ? 8 : 10;
            int w = have_w ? width : INT_MAX;
            int consumed = 0;
            int neg = 0;
            const char* start = p;
            if(conv == 'd' || conv == 'i' || conv == 'u') {
                if(*p == '+' || *p == '-') {
                    if(*p == '-') {
                        neg = 1;
                    }
                    p++;
                    consumed++;
                }
            }
            if(base == 16 && consumed < w && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
                p += 2;
                consumed += 2;
            }
            if(conv == 'i' && consumed == 0 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
                base = 16;
                p += 2;
                consumed += 2;
            }
            unsigned long long val = 0;
            int digits = 0;
            while(*p && consumed < w) {
                int d;
                char c = *p;
                if(c >= '0' && c <= '9') {
                    d = c - '0';
                } else if(c >= 'a' && c <= 'f') {
                    d = c - 'a' + 10;
                } else if(c >= 'A' && c <= 'F') {
                    d = c - 'A' + 10;
                } else {
                    break;
                }
                if(d >= base) {
                    break;
                }
                val = val * base + d;
                digits++;
                p++;
                consumed++;
            }
            if(digits == 0) {
                p = start;
                if(assigned == 0 && !*p) {
                    return -1;
                }
                return assigned;
            }
            if(suppress) {
                continue;
            }
            unsigned long long uv = neg ? (unsigned long long)(-(long long)val) : val;
            if(conv == 'p') {
                *va_arg(ap, void**) = (void*)(size_t)val;
                assigned++;
                continue;
            }
            switch(len) {
            case -2:
                *va_arg(ap, unsigned char*) = (unsigned char)uv;
                break;
            case -1:
                *va_arg(ap, unsigned short*) = (unsigned short)uv;
                break;
            case 1:
                *va_arg(ap, unsigned long*) = (unsigned long)uv;
                break;
            case 2:
                *va_arg(ap, unsigned long long*) = uv;
                break;
            default:
                *va_arg(ap, unsigned int*) = (unsigned int)uv;
                break;
            }
            assigned++;
            continue;
        }
        return assigned;
    }
    return assigned;
}

int __wrap_sscanf(const char* s, const char* f, ...) {
    va_list ap;
    va_start(ap, f);
    int r = __wrap_vsscanf(s, f, ap);
    va_end(ap);
    return r;
}
