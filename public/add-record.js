const form = document.querySelector("form");
const message = form.querySelector("span");
const pathParts = location.pathname.split("/").filter(Boolean);
const endpoint = location.pathname.endsWith("/")
  ? `/${pathParts.at(-1)}`
  : `/${pathParts.at(-1).replace(".html", "")}`;
const collection = endpoint === "/person" ? "/people" : endpoint;
const tableBody = document.querySelector("#records");
const tableColumns = {
  "/person": [["gov_id", "เลขประจำตัวประชาชน"], ["name", "ชื่อ"],
    ["phone", "เบอร์โทรศัพท์"], ["email", "อีเมล"], ["address", "ที่อยู่"]],
  "/equipment_type": [["id", "รหัส"], ["title", "ประเภทอุปกรณ์"]],
  "/problem_type": [["id", "รหัส"], ["title", "ประเภทปัญหา"]],
  "/equipment_vendor": [["id", "รหัส"], ["title", "ผู้ผลิต"]],
  "/equipment_model": [["id", "รหัส"], ["title", "รุ่น"],
    ["vendor", "ผู้ผลิต"], ["equipment_type", "ประเภทอุปกรณ์"]]
};

function escapeHtml(value) {
  return String(value ?? "-").replace(/[&<>"']/g, (character) => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#039;"
  }[character]));
}

async function loadRecords() {
  if (!tableBody) return;
  const response = await fetch(collection);
  const records = await response.json();
  if (!response.ok) throw new Error(records.error || "โหลดข้อมูลไม่สำเร็จ");
  const columns = tableColumns[endpoint];
  tableBody.innerHTML = records.length
    ? records.map((record) => `<tr>${columns.map(([key]) =>
        `<td>${escapeHtml(record[key])}</td>`).join("")}</tr>`).join("")
    : `<tr><td colspan="${columns.length}">ยังไม่มีข้อมูล</td></tr>`;
}

function fill(select, values) {
  const selectedValue = select.value;
  const emptyOption = document.createElement("option");
  emptyOption.value = "";
  emptyOption.textContent = "-- กรุณาเลือก --";
  emptyOption.selected = true;
  emptyOption.disabled = true;
  select.replaceChildren(emptyOption, ...values.map(({ title }) => {
    const option = document.createElement("option");
    option.value = title;
    option.textContent = title;
    return option;
  }));
  if (values.some(({ title }) => title === selectedValue)) {
    select.value = selectedValue;
  }
}

async function loadModelChoices() {
  const response = await fetch("/options");
  if (!response.ok) throw new Error("โหลดข้อมูลไม่สำเร็จ");
  const data = await response.json();
  fill(form.elements.vendor, data.vendors);
  fill(form.elements.equipment_type, data.equipment_types);
}

form.addEventListener("submit", async (event) => {
  event.preventDefault();
  const data = new FormData(form);
  const face = data.get("face");
  if (face instanceof File && face.size) {
    if (face.size > 512 * 1024) {
      message.textContent = "รูปถ่ายต้องมีขนาดไม่เกิน 512 KB";
      return;
    }
    const bytes = new Uint8Array(await face.arrayBuffer());
    let binary = "";
    for (const byte of bytes) binary += String.fromCharCode(byte);
    data.set("face", btoa(binary));
  }
  const response = await fetch(endpoint, {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams(data)
  });
  const result = await response.json();
  message.textContent = result.ok ? "เพิ่มข้อมูลสำเร็จ" : result.error;
  if (result.ok) {
    form.reset();
    loadRecords().catch((error) => { message.textContent = error.message; });
  }
});

if (endpoint === "/equipment_model") {
  loadModelChoices().catch((error) => {
    message.textContent = error.message;
  });
}

loadRecords().catch((error) => { message.textContent = error.message; });
