// Copyright © 2025 TTKB, LLC.
//
// SPDX-License-Identifier: BSD-2-Clause

#ifndef __DEDUP_OUTPUT_FORMAT_H__
#define __DEDUP_OUTPUT_FORMAT_H__

#include <stdint.h>
#include <stdio.h>

// Output format types for byte size formatting
typedef enum OutputFormat {
    // Raw formats
    OUTPUT_RAW,              // Raw bytes (no formatting)
    OUTPUT_RAW_COMMAS,       // Raw bytes with comma separators

    // SI decimal prefixes (1000-based)
    OUTPUT_SI_HUMAN,         // Human readable with SI units (kB, MB, GB, TB)
    OUTPUT_SI_HUMAN_LONG,    // Long form English (kilobytes, megabytes, etc.)

    // Binary prefixes (1024-based)
    OUTPUT_BINARY_HUMAN,     // Human readable with binary units (KiB, MiB, GiB, TiB)
    OUTPUT_BINARY_HUMAN_LONG,// Long form English (kibibytes, mebibytes, etc.)

    // Scientific notation
    OUTPUT_SCIENTIFIC,       // Scientific notation (1.23e+06)
    OUTPUT_SCIENTIFIC_COMMAS,// Scientific with comma separators

    // Traditional disk tool formats
    OUTPUT_DISK_TRADITIONAL, // Traditional disk tool format (like df, du)
    OUTPUT_DISK_TRADITIONAL_LONG, // Long form of traditional

    // Compact formats
    OUTPUT_COMPACT,          // Most compact representation
    OUTPUT_COMPACT_LONG,     // Compact with long units

    // Disk tool specific formats
    OUTPUT_KILO,             // Kilobytes (1000-based, no unit)
    OUTPUT_KIBI,             // Kibibytes (1024-based, no unit)
    OUTPUT_KILO_UNIT,        // Kilobytes with 'k' unit
    OUTPUT_KIBI_UNIT,        // Kibibytes with 'K' unit
    OUTPUT_HUMAN             // Human readable (-h style, SI)
} OutputFormat;

// Format a byte size according to the specified format
// Returns a static buffer that should be used immediately
const char* format_bytes(uint64_t bytes, OutputFormat format);

// Get the default output format
OutputFormat get_default_output_format(void);

// Parse format string to OutputFormat enum
// Returns OUTPUT_SI_HUMAN on invalid format
OutputFormat parse_output_format(const char* format_str);

// Get a description of the format
const char* get_format_description(OutputFormat format);

// List all available formats
void list_available_formats(FILE* out);

// Fixed-width compact format for status line columns
// Always produces exactly 4 characters, right-aligned, SI units (K, M, G, T, P)
// Examples: "   0", " 999", "1.0K", " 99K", "999K", "1.0M", " 99M", "999M", "1.0G", etc.
void format_compact(uint64_t value, char buf[5]);

#endif // __DEDUP_OUTPUT_FORMAT_H__
