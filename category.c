#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <string.h> 
#include <sqlite3.h> 
#include <stdlib.h>
#include <time.h>
#include <stdlib.h>
#include <math.h>
#include <regex.h>
#include <json-c/json.h>

#include <curl/curl.h>
#include "report.h"
#include "import.h"

size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t totalSize = size * nmemb;
    strncat((char *)userp, (char *)contents, totalSize);
    return totalSize;
}

int get_category_id(sqlite3 *db, const char *description) {
    int category_id = -1; // Default to -1 indicating no match found
    CURL *curl;
    CURLcode res;
    struct curl_slist *headers = NULL;
    char *api_key = getenv("OPENAI_API_KEY");
    if (!api_key) {
        fprintf(stderr, "OpenAI API key not set in environment\n");
        return -1;
    }

    curl = curl_easy_init();
    if(curl) {
        const char *MODEL = "gpt-4o-mini";
        char post_data[4096];

        // Retrieve categories and examples from the database
        sqlite3_stmt *stmt;
        char categories_query[] = "SELECT c.label, c.description, e.example FROM categories c LEFT JOIN category_examples e ON c.id = e.category_id";
        int rc = sqlite3_prepare_v2(db, categories_query, -1, &stmt, 0);

        if (rc != SQLITE_OK) {
            fprintf(stderr, "Failed to fetch categories and examples: %s\n", sqlite3_errmsg(db));
            return -1;
        }

        // Construct the prompt dynamically
        char categories_part[2048] = "Categories:\\n";
        char examples_part[2048] = "Examples:\\n";
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *label = (const char *)sqlite3_column_text(stmt, 0);
            const char *description = (const char *)sqlite3_column_text(stmt, 1);
            const char *example = (const char *)sqlite3_column_text(stmt, 2);

            if (label && description) {
                strcat(categories_part, "- ");
                strcat(categories_part, label);
                strcat(categories_part, ": ");
                strcat(categories_part, description);
                strcat(categories_part, "\\n");
            }

            if (example) {
                strcat(examples_part, example);
                strcat(examples_part, "\\n");
            }
        }
        sqlite3_finalize(stmt);

        // Format the JSON request properly
        int written = snprintf(post_data, sizeof(post_data),
            "{"
            "\"model\": \"%s\","
            "\"messages\": [{\"role\": \"user\", \"content\": \""
            "You are a financial assistant that categorizes transactions.\\n"
            "%s"
            "%s"
            "Now classify this transaction:\\n"
            "\\\"%s\\\"\\n"
            "Return only the category name as a string.\"}]}",
            MODEL, categories_part, examples_part, description);

        // Ensure snprintf didn't truncate output
        if (written < 0 || written >= sizeof(post_data)) {
            fprintf(stderr, "Error: JSON request is too long or snprintf failed.\n");
            return 0;
        }

        headers = curl_slist_append(headers, "Content-Type: application/json");
        char auth_header[256];
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
        headers = curl_slist_append(headers, auth_header);

        curl_easy_setopt(curl, CURLOPT_URL, "https://api.openai.com/v1/chat/completions");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);

        res = curl_easy_perform(curl);
        if(res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        } else {
            // Parse the response to extract the category name
            // char buffer[4096];
            char buffer[4096] = {0}; // Initialize buffer to store response
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, buffer);
            res = curl_easy_perform(curl);
            if (res == CURLE_OK) {
                struct json_object *parsed_json;
                struct json_object *choices;
                struct json_object *choice;
                struct json_object *message;
                struct json_object *content;

                parsed_json = json_tokener_parse(buffer);
                json_object_object_get_ex(parsed_json, "choices", &choices);
                choice = json_object_array_get_idx(choices, 0);
                json_object_object_get_ex(choice, "message", &message);
                json_object_object_get_ex(message, "content", &content);
                const char *category_name = json_object_get_string(content);

                // Query the database to get the category_id
                sqlite3_stmt *stmt;
                char *sql = sqlite3_mprintf("SELECT id FROM categories WHERE label = '%q';", category_name);
                int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
                sqlite3_free(sql);

                if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
                    category_id = sqlite3_column_int(stmt, 0);
                }
                sqlite3_finalize(stmt);
                json_object_put(parsed_json); // Free memory
            }
        }

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }

    return category_id;
}

void create_category(const char *label, const char *description) {
    sqlite3 *db;
    char *err_msg = 0;
    int rc = sqlite3_open("budget.db", &db);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        return;
    }

    // Insert new category or update if it exists
    char *sql = sqlite3_mprintf("INSERT INTO categories (label, description) VALUES ('%q', '%q') "
                                "ON CONFLICT(label) DO UPDATE SET description=excluded.description;", label, description);
    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    sqlite3_free(sql);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
    } else {
        printf("Category created or updated: %s with description: %s\n", label, description);
    }

    sqlite3_close(db);
}

