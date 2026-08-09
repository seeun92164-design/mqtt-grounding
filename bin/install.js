#!/usr/bin/env node
// Installs the mqtt-grounding skill into <target>/.claude/skills/mqtt-grounding
// Usage: npx github:seeun92164-design/mqtt-grounding [targetDir]
// targetDir defaults to the current working directory.

const fs = require("fs");
const path = require("path");

const pkgRoot = path.join(__dirname, "..");
const targetBase = process.argv[2] || process.cwd();
const targetDir = path.join(targetBase, ".claude", "skills", "mqtt-grounding");

const ITEMS = ["SKILL.md", "skill.sh", "broker", "web", "board", ".gitignore"];

fs.mkdirSync(targetDir, { recursive: true });

for (const item of ITEMS) {
  const src = path.join(pkgRoot, item);
  const dest = path.join(targetDir, item);
  if (!fs.existsSync(src)) continue;
  fs.cpSync(src, dest, { recursive: true });
}

const skillShPath = path.join(targetDir, "skill.sh");
if (fs.existsSync(skillShPath)) {
  fs.chmodSync(skillShPath, 0o755);
}

console.log(`mqtt-grounding skill installed to ${targetDir}`);
console.log("");
console.log("Next steps:");
console.log(`  cd "${targetDir}"`);
console.log("  ./skill.sh name <yourname>");
console.log("  ./skill.sh check");
console.log("");
console.log("Board firmware template: board/GroundingBoard/");
console.log("  cp board/GroundingBoard/arduino_secrets.h.example board/GroundingBoard/arduino_secrets.h");
console.log("  # fill in SECRET_SSID / SECRET_PASS / DEVICE_NAME / MQTT_HOST, then compile+upload");
