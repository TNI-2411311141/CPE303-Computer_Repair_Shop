#include <fastcgi/fcgiapp.h>
#include <limits.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

static sqlite3 *db;

static void json_string(FCGX_Stream *out, const unsigned char *value);

static void base64_string(FCGX_Stream *out, const unsigned char *data,
                          int size) {
	static const char alphabet[] =
	    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	for (int i = 0; i < size; i += 3) {
		uint32_t block = (uint32_t)data[i] << 16U;
		int remaining = size - i;
		if (remaining > 1)
			block |= (uint32_t)data[i + 1] << 8U;
		if (remaining > 2)
			block |= data[i + 2];
		FCGX_PutChar(alphabet[(block >> 18U) & 63U], out);
		FCGX_PutChar(alphabet[(block >> 12U) & 63U], out);
		FCGX_PutChar(remaining > 1 ? alphabet[(block >> 6U) & 63U] : '=',
		             out);
		FCGX_PutChar(remaining > 2 ? alphabet[block & 63U] : '=', out);
	}
}

static const char *image_mime_type(const unsigned char *data, int size) {
	if (size >= 3 && data[0] == 0xff && data[1] == 0xd8 &&
	    data[2] == 0xff)
		return "image/jpeg";
	if (size >= 8 && !memcmp(data, "\x89PNG\r\n\x1a\n", 8))
		return "image/png";
	if (size >= 6 && (!memcmp(data, "GIF87a", 6) || !memcmp(data, "GIF89a", 6)))
		return "image/gif";
	if (size >= 12 && !memcmp(data, "RIFF", 4) &&
	    !memcmp(data + 8, "WEBP", 4))
		return "image/webp";
	return "application/octet-stream";
}

static const char *status_reason(int status) {
	switch (status) {
	case 200:
		return "OK";
	case 201:
		return "Created";
	case 400:
		return "Bad Request";
	case 405:
		return "Method Not Allowed";
	case 409:
		return "Conflict";
	case 404:
		return "Not Found";
	case 422:
		return "Unprocessable Content";
	case 500:
		return "Internal Server Error";
	default:
		return "Unknown";
	}
}

static void header(FCGX_Request *r, int status) {
	FCGX_FPrintF(r->out,
	             "Status: %d %s\r\n"
	             "Content-Type: application/json; charset=utf-8\r\n\r\n",
	             status, status_reason(status));
}

static void text_header(FCGX_Request *r, int status) {
	FCGX_FPrintF(r->out,
	             "Status: %d %s\r\n"
	             "Content-Type: text/plain; charset=utf-8\r\n\r\n",
	             status, status_reason(status));
}

static void error_response(FCGX_Request *r, const char *message) {
	text_header(r, 400);
	FCGX_PutS(message, r->out);
}

static void error_response_status(FCGX_Request *r, int status,
                                  const char *message) {
	text_header(r, status);
	FCGX_PutS(message, r->out);
}

static void sqlite_error_response(FCGX_Request *r, int status,
                                  const char *context) {
	text_header(r, status);
	FCGX_FPrintF(r->out, "%s: %s (SQLite code %d)", context,
	             sqlite3_errmsg(db), sqlite3_extended_errcode(db));
}

static int sqlite_write_status(int result) {
	int code = sqlite3_extended_errcode(db);
	if (result == SQLITE_CONSTRAINT_UNIQUE ||
	    code == SQLITE_CONSTRAINT_UNIQUE)
		return 409;
	if (((unsigned int)result & 0xffU) == SQLITE_CONSTRAINT ||
	    ((unsigned int)code & 0xffU) == SQLITE_CONSTRAINT)
		return 422;
	return 500;
}

static void json_string(FCGX_Stream *out, const unsigned char *value) {
	const char *s = value ? (const char *)value : "";
	FCGX_PutChar('"', out);
	for (; *s; s++) {
		if (*s == '"' || *s == '\\')
			FCGX_PutChar('\\', out);
		if (*s == '\n') {
			FCGX_PutS("\\n", out);
			continue;
		}
		if (*s == '\r') {
			FCGX_PutS("\\r", out);
			continue;
		}
		if (*s == '\t') {
			FCGX_PutS("\\t", out);
			continue;
		}
		if ((unsigned char)*s < 0x20) {
			FCGX_FPrintF(out, "\\u%04x", (unsigned char)*s);
			continue;
		}
		FCGX_PutChar(*s, out);
	}
	FCGX_PutChar('"', out);
}

static int hex_value(char character) {
	if (character >= '0' && character <= '9')
		return character - '0';
	if (character >= 'A' && character <= 'F')
		return character - 'A' + 10;
	if (character >= 'a' && character <= 'f')
		return character - 'a' + 10;
	return -1;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static char *load_request_body(const char *body, const char *body_file) {
	FILE *file = NULL;
	struct stat file_info;
	char *loaded = NULL;
	size_t body_size;

	if (body && *body)
		return strdup(body);
	if (!body_file || stat(body_file, &file_info) != 0 ||
	    file_info.st_size < 0)
		return strdup("");

	body_size = (size_t)file_info.st_size;
	if (body_size > (size_t)2 * 1024 * 1024)
		return NULL;
	loaded = malloc(body_size + 1);
	if (!loaded)
		return NULL;
	file = fopen(body_file, "rb");
	if (!file || fread(loaded, 1, body_size, file) != body_size) {
		if (file)
			fclose(file);
		free(loaded);
		return NULL;
	}
	fclose(file);
	loaded[body_size] = '\0'; // NOLINT(clang-analyzer-security.ArrayBound)
	return loaded;
}

static int base64_value(char character) {
	if (character >= 'A' && character <= 'Z')
		return character - 'A';
	if (character >= 'a' && character <= 'z')
		return character - 'a' + 26;
	if (character >= '0' && character <= '9')
		return character - '0' + 52;
	if (character == '+')
		return 62;
	if (character == '/')
		return 63;
	return -1;
}

static size_t decode_base64(const char *encoded, unsigned char *decoded) {
	size_t length = strlen(encoded);
	size_t output = 0;
	unsigned int value = 0;
	int bits = -8;

	for (size_t i = 0; i < length; i++) {
		int signed_digit = base64_value(encoded[i]);
		if (signed_digit < 0)
			continue;
		unsigned int digit = (unsigned int)signed_digit;
		value = (value << 6U) | digit;
		bits += 6;
		if (bits >= 0) {
			decoded[output++] =
			    (unsigned char)((value >> (unsigned int)bits) &
			                    0xffU);
			bits -= 8;
		}
	}
	return output;
}

static void decode_form_value(const char *encoded, size_t length, char *decoded,
                              size_t size) {
	size_t output = 0;

	if (!size)
		return;
	for (size_t input = 0; input < length && output + 1 < size; input++) {
		char character = encoded[input];
		if (character == '%' && input + 2 < length) {
			int high = hex_value(encoded[input + 1]);
			int low = hex_value(encoded[input + 2]);
			if (high >= 0 && low >= 0) {
				character = (char)((high * 16) + low);
				input += 2;
			}
		} else if (character == '+') {
			character = ' ';
		}
		decoded[output++] = character;
	}
	decoded[output] = '\0';
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static const char *field(const char *body, const char *name, char *buffer,
                         size_t size) {
	size_t name_length = strlen(name);
	const char *start = body ? body : "";

	if (!size)
		return buffer;
	buffer[0] = '\0';
	while (*start) {
		const char *separator = strchr(start, '&');
		const char *part_end =
		    separator ? separator : start + strlen(start);
		const char *equals =
		    memchr(start, '=', (size_t)(part_end - start));

		if (equals && (size_t)(equals - start) == name_length &&
		    !memcmp(start, name, name_length)) {
			decode_form_value(equals + 1,
			                  (size_t)(part_end - equals - 1),
			                  buffer, size);
			return buffer;
		}
		start = separator ? separator + 1 : part_end;
	}
	return buffer;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static int scalar_id(const char *sql, const char *value, int fallback) {
	sqlite3_stmt *stmt = NULL;
	int id = fallback;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
		sqlite3_bind_text(stmt, 1, value, -1, SQLITE_TRANSIENT);
		if (sqlite3_step(stmt) == SQLITE_ROW)
			id = sqlite3_column_int(stmt, 0);
	}
	sqlite3_finalize(stmt);
	return id;
}

static int record_exists(const char *table, int id) {
	char sql[64];
	sqlite3_stmt *stmt = NULL;
	int exists = 0;
	snprintf(sql, sizeof sql, "SELECT 1 FROM %s WHERE id=?", table);
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
		sqlite3_bind_int(stmt, 1, id);
		exists = sqlite3_step(stmt) == SQLITE_ROW;
	}
	sqlite3_finalize(stmt);
	return exists;
}

static int model_id_for(const char *model_id, const char *vendor,
                        const char *type) {
	sqlite3_stmt *stmt = NULL;
	int id = 0;
	const char *sql = "SELECT m.id FROM equipment_model m "
			  "JOIN equipment_vendor v ON v.id=m.vendor_id "
			  "JOIN equipment_type t ON t.id=m.type_id "
			  "WHERE m.model_id=? AND v.name=? AND t.title=?";
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
		sqlite3_bind_text(stmt, 1, model_id, -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt, 2, vendor, -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt, 3, type, -1, SQLITE_TRANSIENT);
		if (sqlite3_step(stmt) == SQLITE_ROW)
			id = sqlite3_column_int(stmt, 0);
	}
	sqlite3_finalize(stmt);
	return id;
}

static int create_model(const char *model_id, const char *model_name,
                        int vendor_id, int type_id, int *result) {
	sqlite3_stmt *stmt = NULL;
	const char *sql =
	    "INSERT INTO equipment_model(vendor_id,model_id,model_name,type_id) "
	    "VALUES (?,?,?,?)";
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
		*result = SQLITE_ERROR;
		return 0;
	}
	sqlite3_bind_int(stmt, 1, vendor_id);
	sqlite3_bind_text(stmt, 2, model_id, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 3, model_name, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 4, type_id);
	*result = sqlite3_step(stmt);
	int id = *result == SQLITE_DONE ? (int)sqlite3_last_insert_rowid(db) : 0;
	sqlite3_finalize(stmt);
	return id;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static int insert_text(const char *sql, const char *value, int *result) {
	sqlite3_stmt *stmt = NULL;
	int id = 0;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
		sqlite3_bind_text(stmt, 1, value, -1, SQLITE_TRANSIENT);
		*result = sqlite3_step(stmt);
		if (*result == SQLITE_DONE)
			id = (int)sqlite3_last_insert_rowid(db);
	} else {
		*result = SQLITE_ERROR;
	}
	sqlite3_finalize(stmt);
	return id;
}

static void create_record(
    FCGX_Request *r, const char *body,
    const char *action) { // NOLINT(bugprone-easily-swappable-parameters,
                          // readability-function-cognitive-complexity)
	char name[256];
	char gov_id[128];
	char phone[64];
	char email[256];
	char address[512];
	char face_base64[1400000];
	unsigned char face[1050000];
	char title[256];
	char vendor[128];
	char model_id[128];
	char model_name[256];
	char type[128];

	if (!action) {
		error_response(r, "ข้อมูลสำหรับเพิ่มไม่ถูกต้อง");
		return;
	}
	field(body, "name", name, sizeof name);
	field(body, "gov_id", gov_id, sizeof gov_id);
	field(body, "phone", phone, sizeof phone);
	field(body, "email", email, sizeof email);
	field(body, "address", address, sizeof address);
	field(body, "face", face_base64, sizeof face_base64);
	field(body, "title", title, sizeof title);
	field(body, "vendor", vendor, sizeof vendor);
	field(body, "model_id", model_id, sizeof model_id);
	field(body, "model_name", model_name, sizeof model_name);
	field(body, "equipment_type", type, sizeof type);

	int id = 0;
	int insert_result = SQLITE_ERROR;
	if (!strcmp(action, "person")) {
		if (!*name || !*gov_id || !*phone || !*email || !*address ||
		    !*face_base64) {
			error_response(r, "กรุณากรอกข้อมูลบุคคลให้ครบถ้วน");
			return;
		}
		sqlite3_stmt *stmt = NULL;
		size_t face_size = decode_base64(face_base64, face);
		const char *sql =
		    "INSERT INTO person(gov_id,name,face,phone,email,address) "
		    "VALUES (?,?,?,?,?,?)";
		if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
			sqlite3_bind_text(stmt, 1, gov_id, -1,
			                  SQLITE_TRANSIENT);
			sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
			sqlite3_bind_blob(stmt, 3, face, (int)face_size,
			                  SQLITE_TRANSIENT);
			sqlite3_bind_text(stmt, 4, phone, -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(stmt, 5, email, -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(stmt, 6, address, -1,
			                  SQLITE_TRANSIENT);
			insert_result = sqlite3_step(stmt);
			if (insert_result == SQLITE_DONE)
				id = (int)sqlite3_last_insert_rowid(db);
		}
		sqlite3_finalize(stmt);
	} else if (!strcmp(action, "equipment_type") ||
	           !strcmp(action, "problem_type")) {
		if (!*title) {
			error_response(r, "กรุณากรอกชื่อประเภท");
			return;
		}
		const char *sql =
		    !strcmp(action, "equipment_type")
		        ? "INSERT INTO equipment_type(title) VALUES (?)"
		        : "INSERT INTO problem_type(title) VALUES (?)";
		id = insert_text(sql, title, &insert_result);
	} else if (!strcmp(action, "vendor")) {
		if (!*vendor) {
			error_response(r, "กรุณากรอกชื่อผู้ผลิต");
			return;
		}
		id = insert_text(
		    "INSERT INTO equipment_vendor(name) VALUES (?)", vendor,
		    &insert_result);
	} else if (!strcmp(action, "model")) {
		int vendor_id = scalar_id(
		    "SELECT id FROM equipment_vendor WHERE name=?", vendor, 0);
		int type_id = scalar_id(
		    "SELECT id FROM equipment_type WHERE title=?", type, 0);
		if (!*model_id || !*model_name || !vendor_id || !type_id) {
			error_response(r, "กรุณาเลือกผู้ผลิตและประเภทอุปกรณ์ที่มีอยู่");
			return;
		}
		id = create_model(model_id, model_name, vendor_id, type_id,
		                  &insert_result);
	} else {
		error_response(r, "ข้อมูลสำหรับเพิ่มไม่ถูกต้อง");
		return;
	}

	if (!id) {
			if (!strcmp(action, "person") &&
			    insert_result == SQLITE_CONSTRAINT_UNIQUE)
			error_response_status(r, 409,
				                      "เลขประจำตัวประชาชนนี้มีอยู่แล้ว");
			else
				sqlite_error_response(r, sqlite_write_status(insert_result),
				                      "เพิ่มข้อมูล");
		return;
	}
	text_header(r, 201);
	FCGX_PutS("เพิ่มข้อมูลสำเร็จ", r->out);
}

static void list_problems(FCGX_Request *r) {
	sqlite3_stmt *stmt = NULL;
	const char *sql =
	    "SELECT "
	    "p.id,p.title,p.description,p.equipment_serial,p.photo,m.model_name,v.name,"
	    "et.title,pt.title,ps.title,p.fix_cost,c.name,r.name,p.operating_"
	    "system_info,c.id,r.id "
	    "FROM problem p JOIN equipment_model m ON "
	    "m.id=p.equipment_model_id "
	    "JOIN equipment_vendor v ON v.id=m.vendor_id JOIN equipment_type "
	    "et ON et.id=m.type_id "
	    "JOIN problem_type pt ON pt.id=p.type_id JOIN problem_status ps ON "
	    "ps.id=p.status_id "
	    "JOIN person c ON c.id=p.customer_id JOIN person r ON "
	    "r.id=p.respondent_id ORDER BY p.id DESC";
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
		sqlite_error_response(r, 500, "อ่านรายการงานซ่อม");
		return;
	}
	header(r, 200);
	FCGX_PutS("[", r->out);
	int first = 1;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
			if (!first)
				FCGX_PutS(",", r->out);
			first = 0;
			FCGX_FPrintF(r->out, "{\"id\":%d,\"title\":",
			             sqlite3_column_int(stmt, 0));
			json_string(r->out, sqlite3_column_text(stmt, 1));
			FCGX_PutS(",\"description\":", r->out);
			json_string(r->out, sqlite3_column_text(stmt, 2));
			FCGX_PutS(",\"serial\":", r->out);
			json_string(r->out, sqlite3_column_text(stmt, 3));
			FCGX_PutS(",\"photo\":", r->out);
			const unsigned char *photo = sqlite3_column_blob(stmt, 4);
			int photo_size = sqlite3_column_bytes(stmt, 4);
			if (photo && photo_size > 0) {
				FCGX_PutChar('"', r->out);
				FCGX_PutS("data:", r->out);
				FCGX_PutS(image_mime_type(photo, photo_size), r->out);
				FCGX_PutS(";base64,", r->out);
				base64_string(r->out, photo, photo_size);
				FCGX_PutChar('"', r->out);
			} else {
				FCGX_PutS("null", r->out);
			}
			FCGX_PutS(",\"model\":", r->out);
			json_string(r->out, sqlite3_column_text(stmt, 5));
			FCGX_PutS(",\"vendor\":", r->out);
			json_string(r->out, sqlite3_column_text(stmt, 6));
			FCGX_PutS(",\"equipment_type\":", r->out);
			json_string(r->out, sqlite3_column_text(stmt, 7));
			FCGX_PutS(",\"problem_type\":", r->out);
			json_string(r->out, sqlite3_column_text(stmt, 8));
			FCGX_PutS(",\"status\":", r->out);
			json_string(r->out, sqlite3_column_text(stmt, 9));
			FCGX_PutS(",\"charge\":", r->out);
			if (sqlite3_column_type(stmt, 10) == SQLITE_NULL) {
				FCGX_PutS("null", r->out);
			} else {
				FCGX_FPrintF(r->out, "%d",
				             sqlite3_column_int(stmt, 10));
			}
			FCGX_PutS(",\"customer\":", r->out);
			json_string(r->out, sqlite3_column_text(stmt, 11));
			FCGX_FPrintF(r->out, ",\"customer_id\":%d",
			             sqlite3_column_int(stmt, 15));
			FCGX_PutS(",\"respondent\":", r->out);
			json_string(r->out, sqlite3_column_text(stmt, 12));
			FCGX_FPrintF(r->out, ",\"respondent_id\":%d",
			             sqlite3_column_int(stmt, 16));
			FCGX_PutS(",\"os\":", r->out);
			json_string(r->out, sqlite3_column_text(stmt, 13));
			FCGX_PutS("}", r->out);
	}
	sqlite3_finalize(stmt);
	FCGX_PutS("]", r->out);
}

static int path_id(const char *uri, const char *prefix) {
	size_t prefix_length = strlen(prefix);
	if (!uri || strncmp(uri, prefix, prefix_length) != 0)
		return 0;
	char *end = NULL;
	long id = strtol(uri + prefix_length, &end, 10);
	return end != uri + prefix_length && *end == '\0' && id > 0 &&
		id <= INT_MAX
	    ? (int)id
	    : 0;
}

static void get_person(FCGX_Request *r, int id) {
	sqlite3_stmt *stmt = NULL;
	const char *sql =
	    "SELECT gov_id,name,face,phone,email,address FROM person WHERE id=?";
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
		sqlite_error_response(r, 500, "อ่านข้อมูลบุคคล");
		return;
	}
	sqlite3_bind_int(stmt, 1, id);
	if (sqlite3_step(stmt) != SQLITE_ROW) {
		sqlite3_finalize(stmt);
		error_response_status(r, 404, "ไม่พบบุคคล");
		return;
	}
	header(r, 200);
	FCGX_FPrintF(r->out, "{\"id\":%d,\"gov_id\":", id);
	json_string(r->out, sqlite3_column_text(stmt, 0));
	FCGX_PutS(",\"name\":", r->out);
	json_string(r->out, sqlite3_column_text(stmt, 1));
	FCGX_PutS(",\"face\":", r->out);
	const unsigned char *face = sqlite3_column_blob(stmt, 2);
	int face_size = sqlite3_column_bytes(stmt, 2);
	if (face && face_size > 0) {
		FCGX_PutChar('"', r->out);
		FCGX_FPrintF(r->out, "data:%s;base64,",
		             image_mime_type(face, face_size));
		base64_string(r->out, face, face_size);
		FCGX_PutChar('"', r->out);
	} else {
		FCGX_PutS("null", r->out);
	}
	FCGX_PutS(",\"phone\":", r->out);
	json_string(r->out, sqlite3_column_text(stmt, 3));
	FCGX_PutS(",\"email\":", r->out);
	json_string(r->out, sqlite3_column_text(stmt, 4));
	FCGX_PutS(",\"address\":", r->out);
	json_string(r->out, sqlite3_column_text(stmt, 5));
	FCGX_PutS("}", r->out);
	sqlite3_finalize(stmt);
}

static void get_problem(FCGX_Request *r, int id) {
	sqlite3_stmt *stmt = NULL;
	const char *sql =
	    "SELECT p.title,p.description,p.equipment_serial,p.photo,m.model_name,"
	    "v.name,et.title,pt.title,ps.title,p.fix_cost,c.id,c.name,r.id,r.name,"
	    "p.operating_system_info FROM problem p "
	    "JOIN equipment_model m ON m.id=p.equipment_model_id "
	    "JOIN equipment_vendor v ON v.id=m.vendor_id JOIN equipment_type et "
	    "ON et.id=m.type_id JOIN problem_type pt ON pt.id=p.type_id "
	    "JOIN problem_status ps ON ps.id=p.status_id "
	    "JOIN person c ON c.id=p.customer_id JOIN person r ON "
	    "r.id=p.respondent_id WHERE p.id=?";
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
		sqlite_error_response(r, 500, "อ่านข้อมูลงานซ่อม");
		return;
	}
	sqlite3_bind_int(stmt, 1, id);
	if (sqlite3_step(stmt) != SQLITE_ROW) {
		sqlite3_finalize(stmt);
		error_response_status(r, 404, "ไม่พบงานซ่อม");
		return;
	}
	header(r, 200);
	FCGX_FPrintF(r->out, "{\"id\":%d,\"title\":", id);
	json_string(r->out, sqlite3_column_text(stmt, 0));
	FCGX_PutS(",\"description\":", r->out);
	json_string(r->out, sqlite3_column_text(stmt, 1));
	FCGX_PutS(",\"serial\":", r->out);
	json_string(r->out, sqlite3_column_text(stmt, 2));
	FCGX_PutS(",\"photo\":", r->out);
	const unsigned char *photo = sqlite3_column_blob(stmt, 3);
	int photo_size = sqlite3_column_bytes(stmt, 3);
	if (photo && photo_size > 0) {
		FCGX_PutChar('"', r->out);
		FCGX_FPrintF(r->out, "data:%s;base64,",
		             image_mime_type(photo, photo_size));
		base64_string(r->out, photo, photo_size);
		FCGX_PutChar('"', r->out);
	} else {
		FCGX_PutS("null", r->out);
	}
	FCGX_PutS(",\"model\":", r->out);
	json_string(r->out, sqlite3_column_text(stmt, 4));
	FCGX_PutS(",\"vendor\":", r->out);
	json_string(r->out, sqlite3_column_text(stmt, 5));
	FCGX_PutS(",\"equipment_type\":", r->out);
	json_string(r->out, sqlite3_column_text(stmt, 6));
	FCGX_PutS(",\"problem_type\":", r->out);
	json_string(r->out, sqlite3_column_text(stmt, 7));
	FCGX_PutS(",\"status\":", r->out);
	json_string(r->out, sqlite3_column_text(stmt, 8));
	FCGX_PutS(",\"charge\":", r->out);
	if (sqlite3_column_type(stmt, 9) == SQLITE_NULL)
		FCGX_PutS("null", r->out);
	else
		FCGX_FPrintF(r->out, "%d", sqlite3_column_int(stmt, 9));
	FCGX_FPrintF(r->out, ",\"customer_id\":%d,\"customer\":",
	             sqlite3_column_int(stmt, 10));
	json_string(r->out, sqlite3_column_text(stmt, 11));
	FCGX_FPrintF(r->out, ",\"respondent_id\":%d,\"respondent\":",
	             sqlite3_column_int(stmt, 12));
	json_string(r->out, sqlite3_column_text(stmt, 13));
	FCGX_PutS(",\"os\":", r->out);
	json_string(r->out, sqlite3_column_text(stmt, 14));
	FCGX_PutS("}", r->out);
	sqlite3_finalize(stmt);
}

static void update_person(FCGX_Request *r, int id, const char *body) {
	char gov_id[128];
	char name[256];
	char phone[64];
	char email[256];
	char address[512];
	char face_base64[1400000];
	unsigned char face[1050000];
	field(body, "gov_id", gov_id, sizeof gov_id);
	field(body, "name", name, sizeof name);
	field(body, "phone", phone, sizeof phone);
	field(body, "email", email, sizeof email);
	field(body, "address", address, sizeof address);
	field(body, "face", face_base64, sizeof face_base64);
	if (!*gov_id || !*name || !*phone || !*email || !*address) {
		error_response(r, "กรุณากรอกข้อมูลบุคคลให้ครบถ้วน");
		return;
	}
	sqlite3_stmt *stmt = NULL;
	const char *sql = *face_base64
	    ? "UPDATE person SET gov_id=?,name=?,face=?,phone=?,email=?,"
	      "address=? WHERE id=?"
	    : "UPDATE person SET gov_id=?,name=?,phone=?,email=?,address=? "
	      "WHERE id=?";
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
		sqlite_error_response(r, 500, "เตรียมแก้ไขข้อมูลบุคคล");
		return;
	}
	int parameter = 1;
	sqlite3_bind_text(stmt, parameter++, gov_id, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, parameter++, name, -1, SQLITE_TRANSIENT);
	if (*face_base64) {
		size_t face_size = decode_base64(face_base64, face);
		sqlite3_bind_blob(stmt, parameter++, face, (int)face_size,
		                  SQLITE_TRANSIENT);
	}
	sqlite3_bind_text(stmt, parameter++, phone, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, parameter++, email, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, parameter++, address, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, parameter, id);
	int result = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	if (result != SQLITE_DONE) {
		if (result == SQLITE_CONSTRAINT_UNIQUE) {
			error_response_status(r, 409,
			                     "เลขประจำตัวประชาชนนี้มีอยู่แล้ว");
		} else {
			sqlite_error_response(r, sqlite_write_status(result),
			                      "แก้ไขข้อมูลบุคคล");
		}
		return;
	}
	if (!record_exists("person", id)) {
		error_response_status(r, 404, "ไม่พบบุคคล");
		return;
	}
	text_header(r, 200);
	FCGX_PutS("บันทึกสำเร็จ", r->out);
}

static void update_problem_resource(FCGX_Request *r, int id,
                                    const char *body) {
	char title[256];
	char description[2048];
	char serial[256];
	char status[128];
	char os[256];
	char charge[32];
	char respondent[16];
	char photo_base64[1400000];
	unsigned char photo[1050000];
	field(body, "title", title, sizeof title);
	field(body, "description", description, sizeof description);
	field(body, "serial", serial, sizeof serial);
	field(body, "status", status, sizeof status);
	field(body, "respondent_id", respondent, sizeof respondent);
	field(body, "os", os, sizeof os);
	field(body, "charge", charge, sizeof charge);
	field(body, "photo", photo_base64, sizeof photo_base64);
	if (!*title || !*status) {
		error_response(r, "กรุณากรอกข้อมูลงานซ่อมที่จำเป็นให้ครบถ้วน");
		return;
	}
	int status_id =
	    scalar_id("SELECT id FROM problem_status WHERE title=?", status, 0);
	int respondent_id =
	    scalar_id("SELECT id FROM person WHERE id=?", respondent, 0);
	if (!status_id) {
		error_response_status(r, 422, "ไม่พบสถานะงานซ่อม");
		return;
	}
	if (!respondent_id) {
		error_response_status(r, 422, "ไม่พบผู้รับผิดชอบ");
		return;
	}
	sqlite3_stmt *stmt = NULL;
	const char *sql = *photo_base64
	    ? "UPDATE problem SET title=?,description=?,equipment_serial=?,"
	      "respondent_id=?,status_id=?,operating_system_info=?,"
	      "fix_cost=NULLIF(?,''),photo=? WHERE id=?"
	    : "UPDATE problem SET title=?,description=?,equipment_serial=?,"
	      "respondent_id=?,status_id=?,operating_system_info=?,"
	      "fix_cost=NULLIF(?, '') WHERE id=?";
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
		sqlite_error_response(r, 500, "เตรียมแก้ไขงานซ่อม");
		return;
	}
	int parameter = 1;
	sqlite3_bind_text(stmt, parameter++, title, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, parameter++, description, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, parameter++, serial, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, parameter++, respondent_id);
	sqlite3_bind_int(stmt, parameter++, status_id);
	sqlite3_bind_text(stmt, parameter++, os, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, parameter++, charge, -1, SQLITE_TRANSIENT);
	if (*photo_base64) {
		size_t photo_size = decode_base64(photo_base64, photo);
		sqlite3_bind_blob(stmt, parameter++, photo, (int)photo_size,
		                  SQLITE_TRANSIENT);
	}
	sqlite3_bind_int(stmt, parameter, id);
	int result = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	if (result != SQLITE_DONE) {
		sqlite_error_response(r, sqlite_write_status(result), "แก้ไขงานซ่อม");
		return;
	}
	if (!record_exists("problem", id)) {
		error_response_status(r, 404, "ไม่พบงานซ่อม");
		return;
	}
	text_header(r, 200);
	FCGX_PutS("บันทึกสำเร็จ", r->out);
}

static void list_options(FCGX_Stream *out, const char *table) {
	sqlite3_stmt *stmt = NULL;
	int first = 1;
	const char *sql = NULL;
	if (!strcmp(table, "equipment_type")) {
		sql = "SELECT id,title FROM equipment_type ORDER BY id";
	} else if (!strcmp(table, "problem_type")) {
		sql = "SELECT id,title FROM problem_type ORDER BY id";
	} else if (!strcmp(table, "problem_status")) {
		sql = "SELECT id,title FROM problem_status ORDER BY id";
	} else {
		return;
	}
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
		return;
	}
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		if (!first)
			FCGX_PutS(",", out);
		first = 0;
		FCGX_FPrintF(
		    out, "{\"id\":%d,\"title\":", sqlite3_column_int(stmt, 0));
		json_string(out, sqlite3_column_text(stmt, 1));
		FCGX_PutS("}", out);
	}
	sqlite3_finalize(stmt);
}

static void list_people(FCGX_Stream *out) {
	sqlite3_stmt *stmt = NULL;
	if (sqlite3_prepare_v2(db, "SELECT id,name FROM person ORDER BY name",
	                       -1, &stmt, NULL) != SQLITE_OK)
		return;
	int first = 1;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		if (!first)
			FCGX_PutS(",", out);
		first = 0;
		FCGX_FPrintF(
		    out, "{\"id\":%d,\"title\":", sqlite3_column_int(stmt, 0));
		json_string(out, sqlite3_column_text(stmt, 1));
		FCGX_PutS("}", out);
	}
	sqlite3_finalize(stmt);
}

static void list_people_collection(FCGX_Stream *out) {
	sqlite3_stmt *stmt = NULL;
	const char *sql =
	    "SELECT id,gov_id,name,phone,email,address FROM person ORDER BY name";
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
		return;
	int first = 1;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		if (!first)
			FCGX_PutS(",", out);
		first = 0;
		FCGX_FPrintF(out, "{\"id\":%d,\"gov_id\":",
		             sqlite3_column_int(stmt, 0));
		json_string(out, sqlite3_column_text(stmt, 1));
		FCGX_PutS(",\"name\":", out);
		json_string(out, sqlite3_column_text(stmt, 2));
		FCGX_PutS(",\"phone\":", out);
		json_string(out, sqlite3_column_text(stmt, 3));
		FCGX_PutS(",\"email\":", out);
		json_string(out, sqlite3_column_text(stmt, 4));
		FCGX_PutS(",\"address\":", out);
		json_string(out, sqlite3_column_text(stmt, 5));
		FCGX_PutS("}", out);
	}
	sqlite3_finalize(stmt);
}

static void list_vendors(FCGX_Stream *out) {
	sqlite3_stmt *stmt = NULL;
	if (sqlite3_prepare_v2(
		db, "SELECT id,name FROM equipment_vendor ORDER BY name", -1,
		&stmt, NULL) != SQLITE_OK)
		return;
	int first = 1;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		if (!first)
			FCGX_PutS(",", out);
		first = 0;
		FCGX_FPrintF(
		    out, "{\"id\":%d,\"title\":", sqlite3_column_int(stmt, 0));
		json_string(out, sqlite3_column_text(stmt, 1));
		FCGX_PutS("}", out);
	}
	sqlite3_finalize(stmt);
}

static void list_models(FCGX_Stream *out) {
	sqlite3_stmt *stmt = NULL;
	const char *sql =
	    "SELECT m.id,m.model_id,m.model_name,v.name,et.title FROM equipment_model m "
	    "JOIN equipment_vendor v ON v.id=m.vendor_id JOIN equipment_type "
	    "et ON et.id=m.type_id ORDER BY v.name,m.model_name";
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
		return;
	int first = 1;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		if (!first)
			FCGX_PutS(",", out);
		first = 0;
		FCGX_FPrintF(
		    out, "{\"id\":%d,\"title\":", sqlite3_column_int(stmt, 0));
		json_string(out, sqlite3_column_text(stmt, 1));
		FCGX_PutS(",\"model_id\":", out);
		json_string(out, sqlite3_column_text(stmt, 1));
		FCGX_PutS(",\"model_name\":", out);
		json_string(out, sqlite3_column_text(stmt, 2));
		FCGX_PutS(",\"vendor\":", out);
		json_string(out, sqlite3_column_text(stmt, 3));
		FCGX_PutS(",\"equipment_type\":", out);
		json_string(out, sqlite3_column_text(stmt, 4));
		FCGX_PutS("}", out);
	}
	sqlite3_finalize(stmt);
}

static void list_form_options(FCGX_Request *r) {
	header(r, 200);
	FCGX_PutS("{\"people\":[", r->out);
	list_people(r->out);
	FCGX_PutS("],\"vendors\":[", r->out);
	list_vendors(r->out);
	FCGX_PutS("],\"models\":[", r->out);
	list_models(r->out);
	FCGX_PutS("],\"equipment_types\":[", r->out);
	list_options(r->out, "equipment_type");
	FCGX_PutS("],\"problem_types\":[", r->out);
	list_options(r->out, "problem_type");
	FCGX_PutS("],\"statuses\":[", r->out);
	list_options(r->out, "problem_status");
	FCGX_PutS("]}", r->out);
}

static void list_collection(FCGX_Request *r, const char *resource) {
	if (!strcmp(resource, "people")) {
		header(r, 200);
		FCGX_PutS("[", r->out);
		list_people_collection(r->out);
		FCGX_PutS("]", r->out);
		return;
	}
	if (!strcmp(resource, "equipment_type") ||
	    !strcmp(resource, "problem_type")) {
		const char *table = resource;
		header(r, 200);
		FCGX_PutS("[", r->out);
		list_options(r->out, table);
		FCGX_PutS("]", r->out);
		return;
	}
	if (!strcmp(resource, "equipment_vendor")) {
		header(r, 200);
		FCGX_PutS("[", r->out);
		list_vendors(r->out);
		FCGX_PutS("]", r->out);
		return;
	}
	if (!strcmp(resource, "equipment_model")) {
		header(r, 200);
		FCGX_PutS("[", r->out);
		list_models(r->out);
		FCGX_PutS("]", r->out);
		return;
	}
	if (!strcmp(resource, "problems")) {
		list_problems(r);
		return;
	}
	error_response_status(r, 404, "ไม่พบทรัพยากร");
}

static void create_problem(FCGX_Request *r, const char *body) {
	char title[256];
	char description[2048];
	char serial[256];
	char model_id[128];
	char vendor[128];
	char type[128];
	char issue[128];
	char status[128];
	char customer[128];
	char respondent[128];
	char os[256];
	char charge[32];
	char photo_base64[1400000];
	unsigned char photo[1050000];
	field(body, "title", title, sizeof title);
	field(body, "description", description, sizeof description);
	field(body, "serial", serial, sizeof serial);
	field(body, "model", model_id, sizeof model_id);
	field(body, "vendor", vendor, sizeof vendor);
	field(body, "equipment_type", type, sizeof type);
	field(body, "problem_type", issue, sizeof issue);
	field(body, "status", status, sizeof status);
	field(body, "customer", customer, sizeof customer);
	field(body, "respondent", respondent, sizeof respondent);
	field(body, "os", os, sizeof os);
	field(body, "charge", charge, sizeof charge);
	field(body, "photo", photo_base64, sizeof photo_base64);
	if (!*title || !*model_id || !*vendor || !*type || !*issue || !*customer ||
	    !*respondent || !*status || !*photo_base64) {
		error_response(r, "กรุณากรอกข้อมูลที่จำเป็นให้ครบถ้วน");
		return;
	}
	sqlite3_stmt *stmt = NULL;
	int type_id =
	    scalar_id("SELECT id FROM equipment_type WHERE title=?", type, 0);
	int issue_id =
	    scalar_id("SELECT id FROM problem_type WHERE title=?", issue, 0);
	int status_id =
	    scalar_id("SELECT id FROM problem_status WHERE title=?", status, 0);
	int customer_id =
	    scalar_id("SELECT id FROM person WHERE id=?", customer, 0);
	int respondent_id =
	    scalar_id("SELECT id FROM person WHERE id=?", respondent, 0);
	if (!type_id || !issue_id || !status_id || !customer_id ||
	    !respondent_id) {
		error_response_status(
		    r, 422,
		    "กรุณาเพิ่มและเลือกข้อมูลบุคคล ประเภท สถานะ และผู้รับผิดชอบก่อน");
		return;
	}
	int equipment_model_id = model_id_for(model_id, vendor, type);
	if (!equipment_model_id) {
		error_response_status(r, 422,
		                     "กรุณาเพิ่มและเลือกผู้ผลิตกับรุ่นอุปกรณ์ก่อน");
		return;
	}
	const char *sql =
	    "INSERT INTO "
	    "problem(type_id,title,description,equipment_serial,photo,"
	    "equipment_model_id,operating_system_info,respondent_id,customer_"
	    "id,"
	    "status_id,fix_cost) VALUES (?,?,?,?,?,?,?,?,?,?,NULLIF(?,''))";
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
		sqlite_error_response(r, 500, "เตรียมคำสั่งบันทึกงานซ่อม");
		return;
	}
	size_t photo_size = decode_base64(photo_base64, photo);
	sqlite3_bind_int(stmt, 1, issue_id);
	sqlite3_bind_text(stmt, 2, title, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 3, description, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 4, serial, -1, SQLITE_TRANSIENT);
	sqlite3_bind_blob(stmt, 5, photo, (int)photo_size, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 6, equipment_model_id);
	sqlite3_bind_text(stmt, 7, os, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 8, respondent_id);
	sqlite3_bind_int(stmt, 9, customer_id);
	sqlite3_bind_int(stmt, 10, status_id);
	sqlite3_bind_text(stmt, 11, charge, -1, SQLITE_TRANSIENT);
	int insert_result = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	if (insert_result == SQLITE_DONE) {
		text_header(r, 201);
		FCGX_PutS("บันทึกสำเร็จ", r->out);
	} else {
		sqlite_error_response(r, sqlite_write_status(insert_result),
		                     "บันทึกงานซ่อม");
	}
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
int main(int argc, char **argv) {
	if (argc != 2 || FCGX_Init() != 0) {
		return EXIT_FAILURE;
	}
	int socket = FCGX_OpenSocket(argv[1], 1000);
	if (socket < 0)
		return EXIT_FAILURE;
	if (sqlite3_open("computer_repair_shop.sqlite3", &db) != SQLITE_OK) {
		return EXIT_FAILURE;
	}
	sqlite3_extended_result_codes(db, 1);
	sqlite3_exec(db, "PRAGMA foreign_keys=ON", NULL, NULL, NULL);
	sqlite3_exec(db,
	             "ALTER TABLE equipment_model ADD COLUMN model_name TEXT",
	             NULL, NULL, NULL);
	sqlite3_exec(db,
	             "UPDATE equipment_model SET model_name=model_id "
	             "WHERE model_name IS NULL",
	             NULL, NULL, NULL);
	sqlite3_exec(
	    db,
	    "INSERT OR IGNORE INTO problem_status(title) VALUES "
	    "('รับเรื่อง'),('กำลังซ่อม'),('ซ่อมเสร็จ'),('ชำระเงินแล้ว'),('ส่งมอบแล้ว')",
	    NULL, NULL, NULL);
	sqlite3_exec(db,
	             "INSERT OR IGNORE INTO equipment_type(title) VALUES "
	             "('คอมพิวเตอร์'),('โทรศัพท์มือถือ'),('ปริ้นเตอร์')",
	             NULL, NULL, NULL);
	sqlite3_exec(db,
	             "INSERT OR IGNORE INTO problem_type(title) VALUES "
	             "('ถามข้อสงสัย'),('หัวจ่ายหมึกเสีย'),('น้ำเข้าอุปกรณ์'),('วงจรมีปัญหา'"
	             "),('ไฟล์ระบบเสียหาย'),('ลูกค้าไม่ทราบวิธีใช้'),('ติดไวรัส'),('"
	             "โปรแกรมค้าง'),('ติดใบอนุญาต (license)')",
	             NULL, NULL, NULL);
	FCGX_Request request;
	FCGX_InitRequest(&request, socket, 0);
	while (FCGX_Accept_r(&request) >= 0) {
		const char *uri = FCGX_GetParam("REQUEST_URI", request.envp);
		const char *method =
		    FCGX_GetParam("REQUEST_METHOD", request.envp);
		const char *body = FCGX_GetParam("REQUEST_BODY", request.envp);
		const char *body_file =
		    FCGX_GetParam("REQUEST_BODY_FILE", request.envp);
		char *request_body = load_request_body(body, body_file);
		if (!request_body) {
			text_header(&request, 500);
			FCGX_PutS("อ่านข้อมูลคำขอไม่สำเร็จ", request.out);
			FCGX_Finish_r(&request);
			continue;
		}
		if (uri && !strcmp(uri, "/problems") && method &&
		    !strcmp(method, "GET")) {
			list_problems(&request);
		} else if (uri && method && !strcmp(method, "GET") &&
		           path_id(uri, "/problems/")) {
			get_problem(&request, path_id(uri, "/problems/"));
		} else if (uri && method && !strcmp(method, "GET") &&
		           path_id(uri, "/people/")) {
			get_person(&request, path_id(uri, "/people/"));
		} else if (uri && !strcmp(uri, "/options") && method &&
		           !strcmp(method, "GET")) {
			list_form_options(&request);
		} else if (uri && method && !strcmp(method, "GET") &&
		           (!strcmp(uri, "/people") ||
		            !strcmp(uri, "/equipment_type") ||
		            !strcmp(uri, "/equipment_vendor") ||
		            !strcmp(uri, "/equipment_model") ||
		            !strcmp(uri, "/problem_type"))) {
			list_collection(&request, uri + 1);
		} else if (uri &&
		           (!strcmp(uri, "/person") ||
		            !strcmp(uri, "/equipment_type") ||
		            !strcmp(uri, "/problem_type") ||
		            !strcmp(uri, "/equipment_vendor") ||
		            !strcmp(uri, "/equipment_model")) &&
		           method && !strcmp(method, "POST")) {
			const char *action = NULL;
			if (!strcmp(uri, "/person"))
				action = "person";
			else if (!strcmp(uri, "/equipment_type"))
				action = "equipment_type";
			else if (!strcmp(uri, "/problem_type"))
				action = "problem_type";
			else if (!strcmp(uri, "/equipment_vendor"))
				action = "vendor";
			else if (!strcmp(uri, "/equipment_model"))
				action = "model";
			create_record(&request, request_body, action);
		} else if (uri && !strcmp(uri, "/problems") && method &&
		           !strcmp(method, "POST")) {
			create_problem(&request, request_body);
		} else if (uri && method && !strcmp(method, "PATCH") &&
		           path_id(uri, "/people/")) {
			update_person(&request, path_id(uri, "/people/"), request_body);
		} else if (uri && method && !strcmp(method, "PATCH") &&
		           path_id(uri, "/problems/")) {
			update_problem_resource(&request, path_id(uri, "/problems/"),
			                         request_body);
		} else {
			int known_path =
			    uri && (!strcmp(uri, "/options") ||
			            !strcmp(uri, "/problems") ||
			            !strcmp(uri, "/people") ||
			            !strcmp(uri, "/equipment_type") ||
			            !strcmp(uri, "/equipment_vendor") ||
			            !strcmp(uri, "/equipment_model") ||
			            !strcmp(uri, "/problem_type") ||
			            path_id(uri, "/people/") ||
			            path_id(uri, "/problems/"));
			error_response_status(&request, known_path ? 405 : 404,
			                     known_path
			                         ? "ไม่รองรับเมธอดของคำขอนี้"
			                         : "ไม่พบเส้นทางที่ร้องขอ");
		}
		free(request_body);
		FCGX_Finish_r(&request);
	}
	sqlite3_close(db);
	return 0;
}
