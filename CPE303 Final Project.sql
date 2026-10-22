PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS person(
	id INTEGER PRIMARY KEY AUTOINCREMENT,
	gov_id TEXT NOT NULL UNIQUE,
	name TEXT NOT NULL,
	face BLOB,
	phone TEXT NOT NULL,
	email TEXT,
	address TEXT
);

CREATE TABLE IF NOT EXISTS equipment_type(
	id INTEGER PRIMARY KEY AUTOINCREMENT,
	title TEXT NOT NULL UNIQUE
);

CREATE TABLE IF NOT EXISTS equipment_vendor(
	id INTEGER PRIMARY KEY AUTOINCREMENT,
	name TEXT NOT NULL UNIQUE
);

CREATE TABLE IF NOT EXISTS equipment_model(
	id INTEGER PRIMARY KEY AUTOINCREMENT,
	vendor_id INTEGER NOT NULL REFERENCES equipment_vendor(id),
	model_id TEXT NOT NULL,
	type_id INTEGER NOT NULL REFERENCES equipment_type(id),
	UNIQUE(vendor_id, model_id)
);

CREATE TABLE IF NOT EXISTS problem_type(
	id INTEGER PRIMARY KEY AUTOINCREMENT,
	title TEXT NOT NULL UNIQUE
);

CREATE TABLE IF NOT EXISTS problem_status(
	id INTEGER PRIMARY KEY AUTOINCREMENT,
	title TEXT NOT NULL UNIQUE
);

CREATE TABLE IF NOT EXISTS problem(
	id INTEGER PRIMARY KEY AUTOINCREMENT,
	type_id INTEGER NOT NULL REFERENCES problem_type(id),
	title TEXT NOT NULL,
	description TEXT,
	photo BLOB,
	equipment_serial TEXT,
	equipment_model_id INTEGER NOT NULL REFERENCES equipment_model(id),
	operating_system_info TEXT,
	respondent_id INTEGER NOT NULL REFERENCES person(id),
	customer_id INTEGER NOT NULL REFERENCES person(id),
	status_id INTEGER NOT NULL REFERENCES problem_status(id),
	fix_cost INTEGER CHECK (fix_cost IS NULL OR fix_cost >= 0)
);

INSERT OR IGNORE INTO problem_status(title)
	VALUES ('รับเรื่อง'), ('กำลังซ่อม'), ('ซ่อมเสร็จ'), ('ชำระเงินแล้ว'), ('ส่งมอบแล้ว');
INSERT OR IGNORE INTO equipment_type(title)
	VALUES ('คอมพิวเตอร์'), ('โทรศัพท์มือถือ'), ('ปริ้นเตอร์');
INSERT OR IGNORE INTO problem_type(title)
	VALUES ('ถามข้อสงสัย'), ('หัวจ่ายหมึกเสีย'), ('น้ำเข้าอุปกรณ์'),
	       ('วงจรมีปัญหา'), ('ไฟล์ระบบเสียหาย'), ('ลูกค้าไม่ทราบวิธีใช้'),
	       ('ติดไวรัส'), ('โปรแกรมค้าง'), ('ติดใบอนุญาต (license)');
