// Copyright © 2025 TTKB, LLC.
//
// SPDX-License-Identifier: BSD-2-Clause

#include "output_format.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Static buffer for formatted output
static char format_buffer[256];

typedef struct UnitInfo {
    const char* short_name;
    const char* long_name;
    uint64_t divisor;
} UnitInfo;

// SI units (decimal, 1000-based)
static const UnitInfo si_units[] = {
    {"bytes", "bytes", 1},
    {"kB", "kilobytes", 1000ULL},
    {"MB", "megabytes", 1000000ULL},
    {"GB", "gigabytes", 1000000000ULL},
    {"TB", "terabytes", 1000000000000ULL},
    {"PB", "petabytes", 1000000000000000ULL},
    {NULL, NULL, 0}
};

// Binary units (1024-based)
static const UnitInfo binary_units[] = {
    {"bytes", "bytes", 1},
    {"KiB", "kibibytes", 1024ULL},
    {"MiB", "mebibytes", 1048576ULL},
    {"GiB", "gibibytes", 1073741824ULL},
    {"TiB", "tebibytes", 1099511627776ULL},
    {"PiB", "pebibytes", 1125899906842624ULL},
    {NULL, NULL, 0}
};

// Traditional disk tool units (mixed)
static const UnitInfo traditional_units[] = {
    {"B", "bytes", 1},
    {"K", "kilobytes", 1000ULL},
    {"M", "megabytes", 1000000ULL},
    {"G", "gigabytes", 1000000000ULL},
    {"T", "terabytes", 1000000000000ULL},
    {NULL, NULL, 0}
};

static const char* format_with_commas(uint64_t num) {
    static char comma_buffer[32];
    static char temp[32];
    int len = snprintf(temp, sizeof(temp), "%llu", (unsigned long long)num);
    int comma_count = (len - 1) / 3;
    size_t result_len = (size_t)len + (size_t)comma_count;
    char* result = comma_buffer;

    if (result_len >= sizeof(comma_buffer)) {
        return temp; // Fallback if too big
    }

    result[result_len] = '\0';
    int src = len - 1;
    int dst = result_len - 1;

    for (int i = 0; i < len; i++) {
        if (i > 0 && i % 3 == 0) {
            result[dst--] = ',';
        }
        result[dst--] = temp[src--];
    }

    return result;
}

static const char* format_with_units(uint64_t bytes, const UnitInfo* units, int use_long_names) {
    double value = (double)bytes;
    const UnitInfo* unit = units;

    // Find the appropriate unit
    while (unit->short_name && value >= unit[1].divisor) {
        unit++;
    }

    if (unit == units) {
        // Use bytes
        if (use_long_names) {
            snprintf(format_buffer, sizeof(format_buffer), "%llu %s",
                    (unsigned long long)bytes, unit->long_name);
        } else {
            snprintf(format_buffer, sizeof(format_buffer), "%llu %s",
                    (unsigned long long)bytes, unit->short_name);
        }
    } else {
        // Use scaled unit
        value = (double)bytes / (double)unit->divisor;
        if (use_long_names) {
            snprintf(format_buffer, sizeof(format_buffer), "%.1f %s",
                    value, unit->long_name);
        } else {
            snprintf(format_buffer, sizeof(format_buffer), "%.1f%s",
                    value, unit->short_name);
        }
    }

    return format_buffer;
}

const char* format_bytes(uint64_t bytes, OutputFormat format) {
    switch (format) {
        case OUTPUT_RAW:
            snprintf(format_buffer, sizeof(format_buffer), "%llu",
                    (unsigned long long)bytes);
            break;

        case OUTPUT_RAW_COMMAS:
            snprintf(format_buffer, sizeof(format_buffer), "%s",
                    format_with_commas(bytes));
            break;

        case OUTPUT_SI_HUMAN:
            return format_with_units(bytes, si_units, 0);

        case OUTPUT_SI_HUMAN_LONG:
            return format_with_units(bytes, si_units, 1);

        case OUTPUT_BINARY_HUMAN:
            return format_with_units(bytes, binary_units, 0);

        case OUTPUT_BINARY_HUMAN_LONG:
            return format_with_units(bytes, binary_units, 1);

        case OUTPUT_SCIENTIFIC:
            snprintf(format_buffer, sizeof(format_buffer), "%.2e",
                    (double)bytes);
            break;

        case OUTPUT_SCIENTIFIC_COMMAS:
            {
                char temp[64];
                snprintf(temp, sizeof(temp), "%.2e", (double)bytes);
                // For scientific notation with commas, we'd need to parse and format
                // the mantissa. For now, just return scientific.
                snprintf(format_buffer, sizeof(format_buffer), "%s", temp);
            }
            break;

        case OUTPUT_DISK_TRADITIONAL:
            return format_with_units(bytes, traditional_units, 0);

        case OUTPUT_DISK_TRADITIONAL_LONG:
            return format_with_units(bytes, traditional_units, 1);

        case OUTPUT_COMPACT:
            if (bytes < 1000) {
                snprintf(format_buffer, sizeof(format_buffer), "%llu",
                        (unsigned long long)bytes);
            } else if (bytes < 1000000) {
                snprintf(format_buffer, sizeof(format_buffer), "%.0fK",
                        (double)bytes / 1000.0);
            } else if (bytes < 1000000000) {
                snprintf(format_buffer, sizeof(format_buffer), "%.0fM",
                        (double)bytes / 1000000.0);
            } else {
                snprintf(format_buffer, sizeof(format_buffer), "%.0fG",
                        (double)bytes / 1000000000.0);
            }
            break;

        case OUTPUT_COMPACT_LONG:
            if (bytes < 1000) {
                snprintf(format_buffer, sizeof(format_buffer), "%llu bytes",
                        (unsigned long long)bytes);
            } else if (bytes < 1000000) {
                snprintf(format_buffer, sizeof(format_buffer), "%.0f kilobytes",
                        (double)bytes / 1000.0);
            } else if (bytes < 1000000000) {
                snprintf(format_buffer, sizeof(format_buffer), "%.0f megabytes",
                        (double)bytes / 1000000.0);
            } else {
                snprintf(format_buffer, sizeof(format_buffer), "%.0f gigabytes",
                        (double)bytes / 1000000000.0);
            }
            break;

        case OUTPUT_KILO:
            snprintf(format_buffer, sizeof(format_buffer), "%.0f",
                    (double)bytes / 1000.0);
            break;

        case OUTPUT_KIBI:
            snprintf(format_buffer, sizeof(format_buffer), "%.0f",
                    (double)bytes / 1024.0);
            break;

        case OUTPUT_KILO_UNIT:
            snprintf(format_buffer, sizeof(format_buffer), "%.0fk",
                    (double)bytes / 1000.0);
            break;

        case OUTPUT_KIBI_UNIT:
            snprintf(format_buffer, sizeof(format_buffer), "%.0fK",
                    (double)bytes / 1024.0);
            break;

        case OUTPUT_HUMAN:
            return format_with_units(bytes, si_units, 0);

        default:
            return format_bytes(bytes, OUTPUT_SI_HUMAN);
    }

    return format_buffer;
}

OutputFormat get_default_output_format(void) {
    return OUTPUT_SI_HUMAN;
}

OutputFormat parse_output_format(const char* format_str) {
    if (!format_str) return OUTPUT_SI_HUMAN;

    // Raw formats
    if (strcmp(format_str, "raw") == 0) return OUTPUT_RAW;
    if (strcmp(format_str, "raw-commas") == 0) return OUTPUT_RAW_COMMAS;

    // SI formats
    if (strcmp(format_str, "si") == 0 || strcmp(format_str, "human") == 0) return OUTPUT_SI_HUMAN;
    if (strcmp(format_str, "si-long") == 0 || strcmp(format_str, "human-long") == 0) return OUTPUT_SI_HUMAN_LONG;

    // Binary formats
    if (strcmp(format_str, "binary") == 0 || strcmp(format_str, "iec") == 0) return OUTPUT_BINARY_HUMAN;
    if (strcmp(format_str, "binary-long") == 0 || strcmp(format_str, "iec-long") == 0) return OUTPUT_BINARY_HUMAN_LONG;

    // Scientific formats
    if (strcmp(format_str, "scientific") == 0 || strcmp(format_str, "sci") == 0) return OUTPUT_SCIENTIFIC;
    if (strcmp(format_str, "scientific-commas") == 0 || strcmp(format_str, "sci-commas") == 0) return OUTPUT_SCIENTIFIC_COMMAS;

    // Traditional disk tool formats
    if (strcmp(format_str, "traditional") == 0 || strcmp(format_str, "disk") == 0) return OUTPUT_DISK_TRADITIONAL;
    if (strcmp(format_str, "traditional-long") == 0 || strcmp(format_str, "disk-long") == 0) return OUTPUT_DISK_TRADITIONAL_LONG;

    // Compact formats
    if (strcmp(format_str, "compact") == 0) return OUTPUT_COMPACT;
    if (strcmp(format_str, "compact-long") == 0) return OUTPUT_COMPACT_LONG;

    // Disk tool specific formats
    if (strcmp(format_str, "k") == 0) return OUTPUT_KILO;
    if (strcmp(format_str, "K") == 0) return OUTPUT_KIBI;
    if (strcmp(format_str, "k-unit") == 0) return OUTPUT_KILO_UNIT;
    if (strcmp(format_str, "K-unit") == 0) return OUTPUT_KIBI_UNIT;

    // Legacy compatibility
    if (strcmp(format_str, "h") == 0) return OUTPUT_HUMAN;

    return OUTPUT_SI_HUMAN; // Default fallback
}

const char* get_format_description(OutputFormat format) {
    switch (format) {
        case OUTPUT_RAW: return "Raw bytes without formatting";
        case OUTPUT_RAW_COMMAS: return "Raw bytes with comma separators";
        case OUTPUT_SI_HUMAN: return "Human readable with SI units (kB, MB, GB, TB)";
        case OUTPUT_SI_HUMAN_LONG: return "Human readable with long SI units (kilobytes, megabytes, etc.)";
        case OUTPUT_BINARY_HUMAN: return "Human readable with binary units (KiB, MiB, GiB, TiB)";
        case OUTPUT_BINARY_HUMAN_LONG: return "Human readable with long binary units (kibibytes, mebibytes, etc.)";
        case OUTPUT_SCIENTIFIC: return "Scientific notation (1.23e+06)";
        case OUTPUT_SCIENTIFIC_COMMAS: return "Scientific notation with comma separators";
        case OUTPUT_DISK_TRADITIONAL: return "Traditional disk tool format (K, M, G, T)";
        case OUTPUT_DISK_TRADITIONAL_LONG: return "Traditional disk tool format with long names";
        case OUTPUT_COMPACT: return "Most compact representation";
        case OUTPUT_COMPACT_LONG: return "Compact representation with long units";
        case OUTPUT_KILO: return "Kilobytes (1000-based, no unit)";
        case OUTPUT_KIBI: return "Kibibytes (1024-based, no unit)";
        case OUTPUT_KILO_UNIT: return "Kilobytes with 'k' unit";
        case OUTPUT_KIBI_UNIT: return "Kibibytes with 'K' unit";
        case OUTPUT_HUMAN: return "Human readable (-h style, SI units)";
        default: return "Unknown format";
    }
}

// Fixed-width compact format for status line columns
// Always produces exactly 4 characters, right-aligned, SI units
// Examples: "   0", " 999", "1.0K", " 99K", "999K", "1.0M", " 99M", "999M", "1.0G", etc.
void format_compact(uint64_t value, char buf[5]) {
    buf[4] = '\0';  // Always null-terminate at position 4
    
    if (value < 1000) {
        // 0-999: right-aligned integer
        if (value < 10) {
            snprintf(buf, 5, "   %llu", (unsigned long long)value);
        } else if (value < 100) {
            snprintf(buf, 5, "  %2llu", (unsigned long long)value);
        } else {
            snprintf(buf, 5, " %3llu", (unsigned long long)value);
        }
    } else if (value < 10000) {
        // 1000-9999: X.XK (e.g., 1.0K, 9.9K)
        snprintf(buf, 5, "%.1fK", (double)value / 1000.0);
    } else if (value < 100000) {
        // 10000-99999: XXK (e.g., 10K, 99K) space-padded
        snprintf(buf, 5, "%2lluK", (unsigned long long)(value / 1000));
    } else if (value < 1000000) {
        // 100000-999999: XXXK (e.g., 100K, 999K)
        snprintf(buf, 5, "%3lluK", (unsigned long long)(value / 1000));
    } else if (value < 10000000) {
        // 1M-9.9M: X.XM
        snprintf(buf, 5, "%.1fM", (double)value / 1000000.0);
    } else if (value < 100000000) {
        // 10M-99M: XXM (space-padded)
        snprintf(buf, 5, "%2lluM", (unsigned long long)(value / 1000000));
    } else if (value < 1000000000) {
        // 100M-999M: XXXM
        snprintf(buf, 5, "%3lluM", (unsigned long long)(value / 1000000));
    } else if (value < 10000000000ULL) {
        // 1G-9.9G: X.XG
        snprintf(buf, 5, "%.1fG", (double)value / 1000000000.0);
    } else if (value < 100000000000ULL) {
        // 10G-99G: XXG (space-padded)
        snprintf(buf, 5, "%2lluG", (unsigned long long)(value / 1000000000));
    } else if (value < 1000000000000ULL) {
        // 100G-999G: XXXG
        snprintf(buf, 5, "%3lluG", (unsigned long long)(value / 1000000000));
    } else if (value < 10000000000000ULL) {
        // 1T-9.9T: X.XT
        snprintf(buf, 5, "%.1fT", (double)value / 1000000000000.0);
    } else if (value < 100000000000000ULL) {
        // 10T-99T: XXT (space-padded)
        snprintf(buf, 5, "%2lluT", (unsigned long long)(value / 1000000000000ULL));
    } else if (value < 1000000000000000ULL) {
        // 100T-999T: XXXT
        snprintf(buf, 5, "%3lluT", (unsigned long long)(value / 1000000000000ULL));
    } else {
        // 1P+: XXXP (1P = 999P max shown, else wrap)
        if (value >= 1000000000000000ULL) {
            snprintf(buf, 5, "%3lluP", (unsigned long long)(value / 1000000000000000ULL));
        } else {
            // Shouldn't reach here, but fallback
            snprintf(buf, 5, "999P");
        }
    }
    
    // Ensure exactly 4 chars (some snprintf may use fewer for small decimals like 1.0K)
    size_t len = strlen(buf);
    if (len < 4) {
        // Right-align with leading spaces
        size_t pad = 4 - len;
        memmove(buf + pad, buf, len + 1);
        for (size_t i = 0; i < pad; i++) {
            buf[i] = ' ';
        }
    } else if (len > 4) {
        // Shouldn't happen, but truncate
        buf[3] = (value >= 1000) ? buf[3] : '0';
        buf[4] = '\0';
    }
}

void list_available_formats(FILE* out) {
    fprintf(out, "Available output formats:\n");
    fprintf(out, "  raw              - %s\n", get_format_description(OUTPUT_RAW));
    fprintf(out, "  raw-commas       - %s\n", get_format_description(OUTPUT_RAW_COMMAS));
    fprintf(out, "  si, human        - %s\n", get_format_description(OUTPUT_SI_HUMAN));
    fprintf(out, "  si-long, human-long - %s\n", get_format_description(OUTPUT_SI_HUMAN_LONG));
    fprintf(out, "  binary, iec      - %s\n", get_format_description(OUTPUT_BINARY_HUMAN));
    fprintf(out, "  binary-long, iec-long - %s\n", get_format_description(OUTPUT_BINARY_HUMAN_LONG));
    fprintf(out, "  scientific, sci  - %s\n", get_format_description(OUTPUT_SCIENTIFIC));
    fprintf(out, "  scientific-commas, sci-commas - %s\n", get_format_description(OUTPUT_SCIENTIFIC_COMMAS));
    fprintf(out, "  traditional, disk - %s\n", get_format_description(OUTPUT_DISK_TRADITIONAL));
    fprintf(out, "  traditional-long, disk-long - %s\n", get_format_description(OUTPUT_DISK_TRADITIONAL_LONG));
    fprintf(out, "  compact          - %s\n", get_format_description(OUTPUT_COMPACT));
    fprintf(out, "  compact-long     - %s\n", get_format_description(OUTPUT_COMPACT_LONG));
    fprintf(out, "  k                - %s\n", get_format_description(OUTPUT_KILO));
    fprintf(out, "  K                - %s\n", get_format_description(OUTPUT_KIBI));
    fprintf(out, "  k-unit           - %s\n", get_format_description(OUTPUT_KILO_UNIT));
    fprintf(out, "  K-unit           - %s\n", get_format_description(OUTPUT_KIBI_UNIT));
    fprintf(out, "  h                - %s\n", get_format_description(OUTPUT_HUMAN));
}
