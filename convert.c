#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TP3057_TMAX_VPK 2.492

static int alaw_decode(uint8_t a_val) {
    a_val ^= 0x55;  // TP3057 DX output includes even bit inversion.

    int t = (a_val & 0x0F) << 4;
    int seg = (a_val & 0x70) >> 4;

    switch (seg) {
        case 0:
            t += 8;
            break;
        case 1:
            t += 0x108;
            break;
        default:
            t += 0x108;
            t <<= (seg - 1);
            break;
    }

    return (a_val & 0x80) ? t : -t;
}

static int max_abs_decoded_value(void) {
    int max_abs = 0;
    for (int code = 0; code <= 255; ++code) {
        int v = alaw_decode((uint8_t)code);
        int a = (v < 0) ? -v : v;
        if (a > max_abs) {
            max_abs = a;
        }
    }
    return max_abs;
}

static bool is_binary_8bit(const char *s) {
    if (strlen(s) != 8) {
        return false;
    }
    for (size_t i = 0; i < 8; ++i) {
        if (s[i] != '0' && s[i] != '1') {
            return false;
        }
    }
    return true;
}

static bool parse_digital(const char *s, uint8_t *out) {
    char *end = NULL;
    errno = 0;

    if (is_binary_8bit(s)) {
        long v = strtol(s, &end, 2);
        if (errno == 0 && end != NULL && *end == '\0' && v >= 0 && v <= 255) {
            *out = (uint8_t)v;
            return true;
        }
        return false;
    }

    int base = 10;
    if (strlen(s) > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
    }

    long v = strtol(s, &end, base);
    if (errno != 0 || end == s || *end != '\0' || v < 0 || v > 255) {
        return false;
    }

    *out = (uint8_t)v;
    return true;
}

static bool parse_analog(const char *s, double *out) {
    char buf[128];
    size_t n = strlen(s);
    if (n >= sizeof(buf)) {
        return false;
    }
    memcpy(buf, s, n + 1);

    if (n > 0 && (buf[n - 1] == 'V' || buf[n - 1] == 'v')) {
        buf[n - 1] = '\0';
    }

    char *end = NULL;
    errno = 0;
    double v = strtod(buf, &end);
    if (errno != 0 || end == buf || *end != '\0') {
        return false;
    }

    *out = v;
    return true;
}

static bool should_treat_as_analog(const char *s) {
    if (s[0] == '-') {
        return true;
    }
    if (strchr(s, '.') != NULL || strchr(s, 'e') != NULL || strchr(s, 'E') != NULL) {
        return true;
    }
    size_t n = strlen(s);
    if (n > 0 && (s[n - 1] == 'V' || s[n - 1] == 'v')) {
        return true;
    }
    return false;
}

static double digital_to_analog(uint8_t code, int max_linear) {
    int linear = alaw_decode(code);
    return ((double)linear / (double)max_linear) * TP3057_TMAX_VPK;
}

static uint8_t analog_to_digital(double analog, int max_linear) {
    if (analog > TP3057_TMAX_VPK) {
        analog = TP3057_TMAX_VPK;
    } else if (analog < -TP3057_TMAX_VPK) {
        analog = -TP3057_TMAX_VPK;
    }

    double target = analog / TP3057_TMAX_VPK * (double)max_linear;

    uint8_t best_code = 0xD5;
    double best_err = HUGE_VAL;

    for (int code = 0; code <= 255; ++code) {
        int decoded = alaw_decode((uint8_t)code);
        double err = fabs((double)decoded - target);

        if (err < best_err) {
            best_err = err;
            best_code = (uint8_t)code;
        } else if (fabs(err - best_err) < 1e-12) {
            if (target == 0.0 && code == 0xD5) {
                best_code = 0xD5;
            }
        }
    }

    return best_code;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage:\n"
            "  %s <digital_input>\n"
            "  %s <analog_input>\n\n"
            "Examples:\n"
            "  %s 0xAA\n"
            "  %s 10101010\n"
            "  %s 170\n"
            "  %s 0.5\n"
            "  %s -1.2V\n",
            prog, prog, prog, prog, prog, prog, prog);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *input = argv[1];
    int max_linear = max_abs_decoded_value();

    if (should_treat_as_analog(input)) {
        double analog = 0.0;
        if (!parse_analog(input, &analog)) {
            fprintf(stderr, "Invalid analog input: %s\n", input);
            return 1;
        }

        uint8_t code = analog_to_digital(analog, max_linear);
        printf("%s(Analog) -> 0x%02X(Digital)\n", input, code);
        return 0;
    }

    uint8_t code = 0;
    if (parse_digital(input, &code)) {
        double analog = digital_to_analog(code, max_linear);
        printf("%s(Digital) -> %.6f Vpk(Analog)\n", input, analog);
        return 0;
    }

    double analog = 0.0;
    if (parse_analog(input, &analog)) {
        uint8_t out = analog_to_digital(analog, max_linear);
        printf("%s(Analog) -> 0x%02X(Digital)\n", input, out);
        return 0;
    }

    fprintf(stderr, "Invalid input: %s\n", input);
    print_usage(argv[0]);
    return 1;
}
