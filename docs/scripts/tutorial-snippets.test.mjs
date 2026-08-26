import assert from "node:assert/strict";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

import {
  extractTutorialRegions,
  renderTutorialSnippets,
} from "./tutorial-snippets.mjs";

const directory = mkdtempSync(join(tmpdir(), "rift-tutorial-snippets-"));

function rejects(fragment, operation) {
  assert.throws(operation, (error) => {
    assert.match(String(error), new RegExp(fragment));
    return true;
  });
}

try {
  const sourcePath = join(directory, "tutorial-001.cpp");
  const pagePath = join(directory, "tutorial-001.md");
  writeFileSync(
    sourcePath,
    [
      "#include <iostream>",
      "// rift:snippet-begin tutorial-001.first-step",
      "const int answer = 42;",
      "// rift:snippet-end tutorial-001.first-step",
      "// rift:snippet-begin tutorial-001.second-step",
      "std::cout << answer << '\\n';",
      "// rift:snippet-end tutorial-001.second-step",
      "",
    ].join("\n"),
  );

  const markdown = [
    "First:",
    "<!-- rift:snippet tutorial-001.first-step -->",
    "Second:",
    "<!-- rift:snippet tutorial-001.second-step -->",
    "",
  ].join("\n");
  assert.equal(
    renderTutorialSnippets(markdown, {
      filePath: pagePath,
      tutorialDirectory: directory,
    }),
    [
      "First:",
      "```cpp",
      "const int answer = 42;",
      "```",
      "Second:",
      "```cpp",
      "std::cout << answer << '\\n';",
      "```",
      "",
    ].join("\n"),
  );

  assert.deepEqual(
    [...extractTutorialRegions(writeFileAndRead(
      join(directory, "valid.cpp"),
      "// rift:snippet-begin tutorial-001.valid\nint value = 1;\n// rift:snippet-end tutorial-001.valid\n",
    ), "valid.cpp")],
    [["tutorial-001.valid", "int value = 1;"]],
  );

  const invalidSources = [
    ["duplicate", "// rift:snippet-begin tutorial-001.a\nint a;\n// rift:snippet-end tutorial-001.a\n// rift:snippet-begin tutorial-001.a\nint b;\n// rift:snippet-end tutorial-001.a\n"],
    ["nested", "// rift:snippet-begin tutorial-001.a\n// rift:snippet-begin tutorial-001.b\nint a;\n// rift:snippet-end tutorial-001.b\n// rift:snippet-end tutorial-001.a\n"],
    ["without a matching begin", "// rift:snippet-end tutorial-001.a\n"],
    ["unterminated", "// rift:snippet-begin tutorial-001.a\nint a;\n"],
    ["empty", "// rift:snippet-begin tutorial-001.a\n\n// rift:snippet-end tutorial-001.a\n"],
    ["does not match", "// rift:snippet-begin tutorial-001.a\nint a;\n// rift:snippet-end tutorial-001.b\n"],
    ["conditional", "#if 0\n// rift:snippet-begin tutorial-001.a\nint a;\n// rift:snippet-end tutorial-001.a\n#endif\n"],
    ["conditional", "// rift:snippet-begin tutorial-001.a\n#if 0\nint a;\n#endif\n// rift:snippet-end tutorial-001.a\n"],
  ];
  for (const [fragment, source] of invalidSources) {
    rejects(fragment, () => extractTutorialRegions(source, "invalid.cpp"));
  }

  const handwrittenCppFences = [
    "```cpp\nint stale;\n```\n",
    " ```C++\nint stale;\n ```\n",
    "  ````cc title=stale\nint stale;\n  ````\n",
    "   ~~~cxx\nint stale;\n   ~~~\n",
    "~~~~CPP\nint stale;\n~~~~\n",
    "~~~Cc\nint stale;\n~~~\n",
  ];
  for (const fence of handwrittenCppFences) {
    rejects("handwritten C\\+\\+", () =>
      renderTutorialSnippets(fence + markdown, {
        filePath: pagePath,
        tutorialDirectory: directory,
      }),
    );
  }

  for (const fencedDirective of [
    "```text\n<!-- rift:snippet tutorial-001.first-step -->\n```\n",
    "~~~markdown\n<!-- rift:snippet tutorial-001.first-step -->\n~~~\n",
  ]) {
    rejects("inside a Markdown fence", () =>
      renderTutorialSnippets(fencedDirective + markdown, {
        filePath: pagePath,
        tutorialDirectory: directory,
      }),
    );
  }
  rejects("sole content", () =>
    renderTutorialSnippets("prefix <!-- rift:snippet tutorial-001.first-step -->\n", {
      filePath: pagePath,
      tutorialDirectory: directory,
    }),
  );
  rejects("malformed", () =>
    renderTutorialSnippets("<!-- rift:snippet tutorial-001.First_step -->\n", {
      filePath: pagePath,
      tutorialDirectory: directory,
    }),
  );
  rejects("more than once", () =>
    renderTutorialSnippets(
      "<!-- rift:snippet tutorial-001.first-step -->\n<!-- rift:snippet tutorial-001.first-step -->\n",
      { filePath: pagePath, tutorialDirectory: directory },
    ),
  );
  rejects("unknown region", () =>
    renderTutorialSnippets("<!-- rift:snippet tutorial-001.unknown -->\n", {
      filePath: pagePath,
      tutorialDirectory: directory,
    }),
  );
  rejects("belongs to tutorial-001", () =>
    renderTutorialSnippets("<!-- rift:snippet tutorial-002.first-step -->\n", {
      filePath: pagePath,
      tutorialDirectory: directory,
    }),
  );
  rejects("not referenced", () =>
    renderTutorialSnippets("<!-- rift:snippet tutorial-001.first-step -->\n", {
      filePath: pagePath,
      tutorialDirectory: directory,
    }),
  );

  writeFileSync(
    sourcePath,
    "// rift:snippet-begin tutorial-001.fence\n```cpp\n// rift:snippet-end tutorial-001.fence\n",
  );
  rejects("backtick fence", () =>
    renderTutorialSnippets("<!-- rift:snippet tutorial-001.fence -->\n", {
      filePath: pagePath,
      tutorialDirectory: directory,
    }),
  );

  console.log("tutorial snippet parser: all strict extraction checks passed");
} finally {
  rmSync(directory, { recursive: true, force: true });
}

function writeFileAndRead(path, contents) {
  writeFileSync(path, contents);
  return contents;
}
