const fs = require('fs');
const path = require('path');

// Pin the web UI version to the SAME string ESP-IDF uses for the firmware version,
// so AxeOS doesn't show the "Firmware and AxeOS versions do not match" banner.
// ESP-IDF reads the project-root version.txt as PROJECT_VER (the firmware version);
// we read that same file here. Fall back to git describe if it's ever missing.
const rootVersionFile = path.join(__dirname, '..', '..', '..', 'version.txt');

let version;
try {
  version = fs.readFileSync(rootVersionFile, 'utf8').trim();
  if (!version) throw new Error('empty version.txt');
} catch (e) {
  version = require('child_process').execSync('git describe --tags --always --dirty').toString().trim();
}

const outputPath = path.join(__dirname, 'dist', 'axe-os', 'version.txt');
fs.writeFileSync(outputPath, version);

console.log(`Generated ${outputPath} with version ${version}`);
