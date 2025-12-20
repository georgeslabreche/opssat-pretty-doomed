/**
 * Word Error Rate (WER) Calculator
 *
 * WER = (Substitutions + Deletions + Insertions) / Total Reference Words
 *
 * Usage: wer_calculator <reference.txt> <hypothesis.txt>
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_WORDS 1000
#define MAX_WORD_LEN 256

// Convert string to lowercase
void to_lower(char *str) {
    for (int i = 0; str[i]; i++) {
        str[i] = tolower((unsigned char)str[i]);
    }
}

// Remove punctuation from string
void remove_punctuation(char *str) {
    char *src = str, *dst = str;
    while (*src) {
        if (isalnum((unsigned char)*src) || isspace((unsigned char)*src)) {
            *dst++ = *src;
        }
        src++;
    }
    *dst = '\0';
}

// Tokenize string into words
int tokenize(const char *text, char words[][MAX_WORD_LEN]) {
    char *copy = strdup(text);
    if (!copy) return 0;

    to_lower(copy);
    remove_punctuation(copy);

    int count = 0;
    char *token = strtok(copy, " \t\n\r");
    while (token && count < MAX_WORDS) {
        if (strlen(token) > 0) {
            strncpy(words[count], token, MAX_WORD_LEN - 1);
            words[count][MAX_WORD_LEN - 1] = '\0';
            count++;
        }
        token = strtok(NULL, " \t\n\r");
    }

    free(copy);
    return count;
}

// Minimum of three integers
int min3(int a, int b, int c) {
    int min = a;
    if (b < min) min = b;
    if (c < min) min = c;
    return min;
}

// Calculate Levenshtein distance between word arrays
int levenshtein_words(char ref[][MAX_WORD_LEN], int ref_len,
                      char hyp[][MAX_WORD_LEN], int hyp_len) {
    // Allocate DP table
    int **dp = malloc((ref_len + 1) * sizeof(int *));
    for (int i = 0; i <= ref_len; i++) {
        dp[i] = malloc((hyp_len + 1) * sizeof(int));
    }

    // Initialize base cases
    for (int i = 0; i <= ref_len; i++) dp[i][0] = i;  // Deletions
    for (int j = 0; j <= hyp_len; j++) dp[0][j] = j;  // Insertions

    // Fill DP table
    for (int i = 1; i <= ref_len; i++) {
        for (int j = 1; j <= hyp_len; j++) {
            int cost = (strcmp(ref[i-1], hyp[j-1]) == 0) ? 0 : 1;
            dp[i][j] = min3(
                dp[i-1][j] + 1,      // Deletion
                dp[i][j-1] + 1,      // Insertion
                dp[i-1][j-1] + cost  // Substitution
            );
        }
    }

    int result = dp[ref_len][hyp_len];

    // Free DP table
    for (int i = 0; i <= ref_len; i++) {
        free(dp[i]);
    }
    free(dp);

    return result;
}

// Read file contents into buffer
char* read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buffer = malloc(size + 1);
    if (!buffer) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(buffer, 1, size, f);
    buffer[read] = '\0';
    fclose(f);

    return buffer;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <reference.txt> <hypothesis.txt>\n", argv[0]);
        return 1;
    }

    // Read reference text
    char *ref_text = read_file(argv[1]);
    if (!ref_text) {
        fprintf(stderr, "Error: Cannot read reference file: %s\n", argv[1]);
        return 1;
    }

    // Read hypothesis text
    char *hyp_text = read_file(argv[2]);
    if (!hyp_text) {
        fprintf(stderr, "Error: Cannot read hypothesis file: %s\n", argv[2]);
        free(ref_text);
        return 1;
    }

    // Tokenize
    static char ref_words[MAX_WORDS][MAX_WORD_LEN];
    static char hyp_words[MAX_WORDS][MAX_WORD_LEN];

    int ref_count = tokenize(ref_text, ref_words);
    int hyp_count = tokenize(hyp_text, hyp_words);

    free(ref_text);
    free(hyp_text);

    if (ref_count == 0) {
        fprintf(stderr, "Error: Reference text is empty\n");
        return 1;
    }

    // Calculate WER
    int edit_distance = levenshtein_words(ref_words, ref_count, hyp_words, hyp_count);
    float wer = (float)edit_distance / ref_count;

    // Output results
    printf("Reference words: %d\n", ref_count);
    printf("Hypothesis words: %d\n", hyp_count);
    printf("Edit distance: %d\n", edit_distance);
    printf("WER: %.2f%% (%d / %d)\n", wer * 100, edit_distance, ref_count);

    // Print word lists for debugging
    printf("\nReference: ");
    for (int i = 0; i < ref_count; i++) {
        printf("%s ", ref_words[i]);
    }
    printf("\n");

    printf("Hypothesis: ");
    for (int i = 0; i < hyp_count; i++) {
        printf("%s ", hyp_words[i]);
    }
    printf("\n");

    return 0;
}
