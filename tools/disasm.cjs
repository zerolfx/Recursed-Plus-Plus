// Print a bounded range from the locally generated dumpbin disassembly.
const fs = require('node:fs');
const low = parseInt(process.argv[2], 16), high = parseInt(process.argv[3], 16);
if (!Number.isFinite(low) || !Number.isFinite(high) || high <= low) process.exit(2);
for (const line of fs.readFileSync('analysis/recursed-disassembly.txt', 'utf8').split(/\r?\n/)) {
  const match = /^  ([0-9A-F]{8}):/.exec(line);
  if (match) { const address = parseInt(match[1], 16); if (address >= low && address < high) console.log(line); }
}
