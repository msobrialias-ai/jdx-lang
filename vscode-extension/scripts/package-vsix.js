#!/usr/bin/env node
const fs = require('fs');
const path = require('path');
const cp = require('child_process');
const metadata = require('../package.json');

const root = path.resolve(__dirname, '..');
const out = path.join(root, `jdx-language-tools-${metadata.version}.vsix`);

function run(cmd, args, cwd) {
  cp.execFileSync(cmd, args, { cwd, stdio: 'inherit' });
}

try {
  run('zip', ['-r', out, '.', '-x', `jdx-language-tools-${metadata.version}.vsix`], root);
  console.log(`Created ${out}`);
} catch (err) {
  console.error(err.message);
  process.exit(1);
}
