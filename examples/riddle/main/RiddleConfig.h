// SPDX-License-Identifier: GPL-2.0-only

#pragma once

// Keep local credentials in RiddleSecrets.h. That file is intentionally
// optional so the example builds and runs in offline demo mode by default.
#if defined(__has_include)
#if __has_include("RiddleSecrets.h")
#include "RiddleSecrets.h"
#endif
#endif

#ifndef RIDDLE_WIFI_SSID
#define RIDDLE_WIFI_SSID ""
#endif

#ifndef RIDDLE_WIFI_PASSWORD
#define RIDDLE_WIFI_PASSWORD ""
#endif

#ifndef RIDDLE_OPENAI_API_KEY
#define RIDDLE_OPENAI_API_KEY ""
#endif

#ifndef RIDDLE_OPENAI_BASE_URL
#define RIDDLE_OPENAI_BASE_URL "https://api.openai.com/v1"
#endif

#ifndef RIDDLE_OPENAI_MODEL
#define RIDDLE_OPENAI_MODEL "gpt-4o-mini"
#endif

#ifndef RIDDLE_OPENAI_MAX_TOKENS
#define RIDDLE_OPENAI_MAX_TOKENS 320
#endif

// Supply a PEM root certificate here to validate the endpoint. Insecure TLS
// must be enabled explicitly and should only be used for a bench test.
#ifndef RIDDLE_OPENAI_ROOT_CA
#define RIDDLE_OPENAI_ROOT_CA ""
#endif

#ifndef RIDDLE_OPENAI_INSECURE_TLS
#define RIDDLE_OPENAI_INSECURE_TLS 0
#endif

#ifndef RIDDLE_WIFI_CONNECT_TIMEOUT_MS
#define RIDDLE_WIFI_CONNECT_TIMEOUT_MS 20000UL
#endif

#ifndef RIDDLE_ORACLE_CONNECT_TIMEOUT_MS
#define RIDDLE_ORACLE_CONNECT_TIMEOUT_MS 15000UL
#endif

#ifndef RIDDLE_ORACLE_TOTAL_TIMEOUT_MS
#define RIDDLE_ORACLE_TOTAL_TIMEOUT_MS 120000UL
#endif

#ifndef RIDDLE_NTP_SYNC_TIMEOUT_MS
#define RIDDLE_NTP_SYNC_TIMEOUT_MS 15000UL
#endif

#ifndef RIDDLE_ORACLE_RESPONSE_SCAN_BYTES
#define RIDDLE_ORACLE_RESPONSE_SCAN_BYTES 32768UL
#endif

#ifndef RIDDLE_ORACLE_REPLY_BYTES
#define RIDDLE_ORACLE_REPLY_BYTES 768
#endif

#ifndef RIDDLE_ORACLE_ERROR_BYTES
#define RIDDLE_ORACLE_ERROR_BYTES 192
#endif

#ifndef RIDDLE_ORACLE_TASK_STACK_BYTES
#define RIDDLE_ORACLE_TASK_STACK_BYTES 16384
#endif

#ifndef RIDDLE_ORACLE_TASK_PRIORITY
#define RIDDLE_ORACLE_TASK_PRIORITY 1
#endif

#ifndef RIDDLE_ORACLE_TASK_CORE
#define RIDDLE_ORACLE_TASK_CORE 0
#endif
