import { readdir, readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { fileURLToPath } from "node:url";

const distDirectory = fileURLToPath(new URL("../dist", import.meta.url));

// Sourcey 3.6.5 receives these directives from its Doxygen adapter after its
// normal Markdown-directive pass, so convert them to Sourcey's callout markup.
const calloutPattern = /<p>:::(warning|note)\s*\n([\s\S]*?)<p>:::<\/p>/g;

let transformedCallouts = 0;

async function transformDirectory(directory) {
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    const path = join(directory, entry.name);

    if (entry.isDirectory()) {
      await transformDirectory(path);
      continue;
    }

    if (!entry.isFile() || !entry.name.endsWith(".html")) {
      continue;
    }

    const html = await readFile(path, "utf8");
    const transformed = html.replace(calloutPattern, (_, kind, body) => {
      ++transformedCallouts;
      const title = kind[0].toUpperCase() + kind.slice(1);
      return `<div class="callout callout-${kind} not-prose">
<div class="callout-title">${title}</div>
<div class="callout-content"><p>${body.trimStart()}</div>
</div>`;
    });

    if (transformed !== html) {
      await writeFile(path, transformed);
    }
  }
}

await transformDirectory(distDirectory);
console.log(`Rendered ${transformedCallouts} Doxygen callout(s).`);
