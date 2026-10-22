const form = document.querySelector("form");
const message = form.querySelector("span");
const endpoint = `/${location.pathname.split("/").pop().replace(".html", "")}`;

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
  }
});

if (endpoint === "/equipment_model") {
  loadModelChoices().catch((error) => {
    message.textContent = error.message;
  });
}
