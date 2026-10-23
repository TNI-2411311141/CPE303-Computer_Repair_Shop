# ความต้องการขององค์กร (เขียน business rule)
ร้านซ่อมคอมพิวเตอร์ของภาคิน เป็นร้านซ่อมอุปกรณ์ไอทีทั่วไป โดยมีช่องทางการรับซ่อมเป็นหน้าร้านเท่านั้น โดยร้านมีการบริหารโดยคนเพียงคนเดียว โดยทางร้านต้องการตรวจสอบข้อมูลและทำสถิติเกี่ยวกับการซ่อม โดยมีข้อมูลที่ต้องการตรวจสอบว่า ปัญหาที่เจอคืออะไร ปัญหาจัดอยู่ประเภทใด อุปกรณ์ใดที่เป็นปัญหา ข้อมูลระบุอุปกรณ์ มีอุปกรณ์ใดที่ยังอยู่ในบริการหรือส่งมอบคืนลูกค้าแล้ว ใครเป็นผู้รับผิดชอบหลักของปัญหา และอื่นๆที่เกี่ยวข้อง โดยคำนึงถึงความถูกต้องของการดำเนินธุรกิจที่มีต่อลูกค้า

# การตีแผ่ความต้องการเป็นหน่วยย่อย (อาจไม่สมบูรณ์เนื่องจากไม่ได้มีการลงหน้างานจริง)
ความต้องการหลักขององค์กรคือ ระบบตั๋วงาน
## ประเภทของอุปกรณ์
- คอมพิวเตอร์
- โทรศัพท์มือถือ
- ปริ้นเตอร์
## ประเภทของปัญหา
- ถามข้อสงสัย
- ฮาร์ดแวร์
	- หัวจ่ายหมึกเสีย
	- น้ำเข้าอุปกรณ์
	- วงจรมีปัญหา
- ซอฟต์แวร์
	- ไฟล์ระบบเสียหาย
	- ลูกค้าไม่ทราบวิธีใช้
	- ติดไวรัส
	- โปรแกรมค้าง
	- ติดใบอนุญาติ (license)
## ข้อมูลอื่นๆ
- รายละเอียดปัญหา
- ผู้ผลิต
- รุ่น
- รหัสประจำเครื่อง
- รูปถ่ายเครื่อง
- ระบบปฎิบัติการ
- ผู้รับผิดชอบ
- ลูกค้า
- สถานะปัจจุบันของการซ่อม
- ราคาซ่อม
## ข้อมูลบุคคล
- ข้อมูลยืนตันบุคคล
	- หมายเลขบัตรประจำตัวประชาชน
	- ชื่อเต็ม
	- รูปถ่ายหน้าตรง
- ข้อมูลติดต่อ
	- ที่อยู่อิเล็กทรอนิกส์
	- เบอร์โทรศัพท์
	- ที่อยู่ (อาจมีหรือไม่ก็ได้)

# กำหนด entity
- problem
- problem_type
- problem_status
- equipment_model
- equipment_type
- equipment_vendor
- person

# กำหนดความสัมพันธ์ระหว่าง entity
- problem > has a customer - person
- problem > has a respondent - person
- problem > has an equipment model - equipment_model
- problem > classified as - problem_type
- problem > has a status - problem_status
- equipment_model > classified as - equipment_type
- equipment_model > produced by - equipment_vendor

# กำหนด attribute ให้กับแต่ะละ entity

- problem (id, type_id, title, description, photo, equipment_serial, equipment_model_id, operating_system_info, respondent_id, customer_id, status_id, fix_cost)
- problem_type (id, title)
- problem_status (id, title)
- equipment_model (id, vendor_id, model_id, model_name, type_id)
- equipment_type (id, title)
- equipment_vendor (id, name)
- person (id, gov_id, name, face, phone, email, address)

# วาดรูป ER-diagram (Chen หรือ crow’s foot)
![ER Diagram](CPE303%20Final%20Project%20ER.svg)

# Data Dictionary

## คำอธิบายชนิดข้อมูลและสัญลักษณ์
- `INTEGER` ใช้เก็บจำนวนเต็มและรหัสระเบียน
- `TEXT` ใช้เก็บข้อความ
- `BLOB` ใช้เก็บข้อมูลไบนารี เช่น รูปภาพ
- `PK` (Primary Key) คือคีย์หลักของตาราง
- `FK` (Foreign Key) คือคีย์ที่อ้างอิงไปยังตารางอื่น
- `NOT NULL` คือข้อมูลที่ต้องมีค่า
- `UNIQUE` คือข้อมูลที่ห้ามซ้ำกัน
- `NULL` คือไม่มีข้อมูลหรือไม่เกี่ยวข้อง

## ตาราง `person` — ข้อมูลบุคคล

| ชื่อฟิลด์ | ชนิดข้อมูล | คีย์/ข้อจำกัด | คำอธิบาย |
|---|---|---|---|
| `id` | INTEGER | PK, AUTOINCREMENT | รหัสบุคคลที่ระบบสร้างให้อัตโนมัติ |
| `gov_id` | TEXT | NOT NULL, UNIQUE | หมายเลขบัตรประจำตัวประชาชนหรือรหัสยืนยันตัวบุคคล |
| `name` | TEXT | NOT NULL | ชื่อ-นามสกุลของบุคคล |
| `face` | BLOB | NULL ได้ | รูปถ่ายหน้าตรงของบุคคล |
| `phone` | TEXT | NOT NULL | หมายเลขโทรศัพท์สำหรับติดต่อ |
| `email` | TEXT | NULL ได้ | ที่อยู่อีเมล |
| `address` | TEXT | NULL ได้ | ที่อยู่ของบุคคล |

บุคคลในตารางนี้อาจเป็นลูกค้า (`problem.customer_id`) หรือผู้รับผิดชอบงาน
(`problem.respondent_id`)

## ตาราง `equipment_type` — ประเภทอุปกรณ์

| ชื่อฟิลด์ | ชนิดข้อมูล | คีย์/ข้อจำกัด | คำอธิบาย |
|---|---|---|---|
| `id` | INTEGER | PK, AUTOINCREMENT | รหัสประเภทอุปกรณ์ |
| `title` | TEXT | NOT NULL, UNIQUE | ชื่อประเภทอุปกรณ์ เช่น คอมพิวเตอร์ โทรศัพท์มือถือ หรือปริ้นเตอร์ |

## ตาราง `equipment_vendor` — ผู้ผลิตอุปกรณ์

| ชื่อฟิลด์ | ชนิดข้อมูล | คีย์/ข้อจำกัด | คำอธิบาย |
|---|---|---|---|
| `id` | INTEGER | PK, AUTOINCREMENT | รหัสผู้ผลิต |
| `name` | TEXT | NOT NULL, UNIQUE | ชื่อบริษัทหรือยี่ห้อผู้ผลิตอุปกรณ์ |

## ตาราง `equipment_model` — รุ่นอุปกรณ์

| ชื่อฟิลด์ | ชนิดข้อมูล | คีย์/ข้อจำกัด | คำอธิบาย |
|---|---|---|---|
| `id` | INTEGER | PK, AUTOINCREMENT | รหัสรุ่นอุปกรณ์ในระบบ |
| `vendor_id` | INTEGER | NOT NULL, FK → `equipment_vendor.id` | รหัสผู้ผลิตของรุ่นอุปกรณ์ |
| `model_id` | TEXT | NOT NULL | รหัสรุ่นหรือรหัสโมเดลจากผู้ผลิต |
| `model_name` | TEXT | NOT NULL | ชื่อรุ่นอุปกรณ์ที่ใช้แสดงผล |
| `type_id` | INTEGER | NOT NULL, FK → `equipment_type.id` | รหัสประเภทอุปกรณ์ |

ข้อจำกัดเพิ่มเติม: คู่ค่า `vendor_id` และ `model_id` ต้องไม่ซ้ำกัน

## ตาราง `problem_type` — ประเภทปัญหา

| ชื่อฟิลด์ | ชนิดข้อมูล | คีย์/ข้อจำกัด | คำอธิบาย |
|---|---|---|---|
| `id` | INTEGER | PK, AUTOINCREMENT | รหัสประเภทปัญหา |
| `title` | TEXT | NOT NULL, UNIQUE | ชื่อประเภทปัญหา เช่น ฮาร์ดแวร์ ซอฟต์แวร์ หรือถามข้อสงสัย |

## ตาราง `problem_status` — สถานะงานซ่อม

| ชื่อฟิลด์ | ชนิดข้อมูล | คีย์/ข้อจำกัด | คำอธิบาย |
|---|---|---|---|
| `id` | INTEGER | PK, AUTOINCREMENT | รหัสสถานะงานซ่อม |
| `title` | TEXT | NOT NULL, UNIQUE | ชื่อสถานะปัจจุบันของงาน |

สถานะเริ่มต้นที่ระบบเตรียมไว้ ได้แก่ `รับเรื่อง`, `กำลังซ่อม`, `ซ่อมเสร็จ`,
`ชำระเงินแล้ว` และ `ส่งมอบแล้ว`

## ตาราง `problem` — ตั๋วงานซ่อม

| ชื่อฟิลด์ | ชนิดข้อมูล | คีย์/ข้อจำกัด | คำอธิบาย |
|---|---|---|---|
| `id` | INTEGER | PK, AUTOINCREMENT | เลขที่ตั๋วงานซ่อม |
| `type_id` | INTEGER | NOT NULL, FK → `problem_type.id` | ประเภทของปัญหา |
| `title` | TEXT | NOT NULL | หัวข้อหรือชื่อย่อของปัญหา |
| `description` | TEXT | NULL ได้ | รายละเอียดอาการปัญหาและข้อมูลเพิ่มเติมจากลูกค้า |
| `photo` | BLOB | NULL ได้ | รูปถ่ายอุปกรณ์หรือหลักฐานอาการเสีย |
| `equipment_serial` | TEXT | NULL ได้ | รหัสประจำเครื่องหรือหมายเลขซีเรียล |
| `equipment_model_id` | INTEGER | NOT NULL, FK → `equipment_model.id` | รุ่นของอุปกรณ์ที่นำมาซ่อม |
| `operating_system_info` | TEXT | NULL ได้ | ข้อมูลระบบปฏิบัติการของอุปกรณ์ |
| `respondent_id` | INTEGER | NOT NULL, FK → `person.id` | รหัสผู้รับผิดชอบหลักของงาน |
| `customer_id` | INTEGER | NOT NULL, FK → `person.id` | รหัสลูกค้าที่นำอุปกรณ์เข้ารับบริการ |
| `status_id` | INTEGER | NOT NULL, FK → `problem_status.id` | สถานะปัจจุบันของงานซ่อม |
| `fix_cost` | INTEGER | NULL ได้, CHECK ≥ 0 | ค่าใช้จ่ายในการซ่อม หน่วยเป็นบาท |

`fix_cost` อนุญาตให้เป็น `NULL` ได้เมื่อยังไม่ประเมินราคา และหากมีค่าต้องไม่เป็น
จำนวนติดลบ

# เขียนเว็บ สำหรับนำข้อมูลเข้าฐานข้อมูล อ่านข้อมูล แก้ไขข้อมูล  ในฐานข้อมูล
- Backend server is FastCGI script
- Frontend is vanilla HTML/JS/CSS
- Database is sqlite3
- HTTP Server is Nginx
- Infrastructure is VPS on that visperhost
