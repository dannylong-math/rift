import { readFileSync, readdirSync } from "node:fs";
import { basename, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const regionName = "tutorial-[0-9]{3}\\.[a-z0-9][a-z0-9-]*";
const beginPattern = new RegExp(`^\\s*// rift:snippet-begin (${regionName})$`);
const endPattern = new RegExp(`^\\s*// rift:snippet-end (${regionName})$`);
const directivePattern = new RegExp(`^<!-- rift:snippet (${regionName}) -->$`);
const conditionalPattern = /^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b/;
const cppFenceLanguages = new Set(["cpp", "c++", "cc", "cxx"]);
const defaultTutorialDirectory = fileURLToPath(
  new URL("../../tutorials/", import.meta.url),
);

function inspectMarkdownFences(body, filePath) {
  let activeFence = null;

  for (const [index, line] of body.split("\n").entries()) {
    const lineNumber = index + 1;
    if (activeFence !== null) {
      const closing = line.match(/^( {0,3})(`{3,}|~{3,})[\t ]*$/);
      if (
        closing &&
        closing[2][0] === activeFence.character &&
        closing[2].length >= activeFence.length
      ) {
        activeFence = null;
        continue;
      }
      if (line.includes("rift:snippet")) {
        throw new Error(
          `${filePath}:${lineNumber}: tutorial snippet directive is inside a Markdown fence`,
        );
      }
      continue;
    }

    const opening = line.match(/^( {0,3})(`{3,}|~{3,})(.*)$/);
    if (!opening || (opening[2][0] === "`" && opening[3].includes("`"))) {
      continue;
    }
    if (line.includes("rift:snippet")) {
      throw new Error(
        `${filePath}:${lineNumber}: tutorial snippet directive is inside a Markdown fence`,
      );
    }
    const language = opening[3].trim().split(/\s+/, 1)[0].toLowerCase();
    if (cppFenceLanguages.has(language)) {
      throw new Error(
        `${filePath}:${lineNumber}: handwritten C++ fences are forbidden on tutorial pages`,
      );
    }
    activeFence = {
      character: opening[2][0],
      length: opening[2].length,
    };
  }
}

export function extractTutorialRegions(source, sourcePath) {
  const regions = new Map();
  let active = null;
  let lines = [];
  let conditionalDepth = 0;

  for (const [index, line] of source.split("\n").entries()) {
    const lineNumber = index + 1;
    const conditional = line.match(conditionalPattern);
    const begin = line.match(beginPattern);
    const end = line.match(endPattern);

    if (line.includes("rift:snippet-") && !begin && !end) {
      throw new Error(`${sourcePath}:${lineNumber}: malformed tutorial snippet marker`);
    }
    if ((begin || end) && conditionalDepth !== 0) {
      throw new Error(`${sourcePath}:${lineNumber}: tutorial marker is inside a conditional region`);
    }
    if (conditional && active !== null) {
      throw new Error(`${sourcePath}:${lineNumber}: tutorial region contains a conditional directive`);
    }

    if (begin) {
      if (active !== null) {
        throw new Error(`${sourcePath}:${lineNumber}: nested tutorial region ${begin[1]}`);
      }
      if (regions.has(begin[1])) {
        throw new Error(`${sourcePath}:${lineNumber}: duplicate tutorial region ${begin[1]}`);
      }
      active = begin[1];
      lines = [];
    } else if (end) {
      if (active === null) {
        throw new Error(`${sourcePath}:${lineNumber}: tutorial region end without a matching begin`);
      }
      if (end[1] !== active) {
        throw new Error(`${sourcePath}:${lineNumber}: tutorial region end does not match ${active}`);
      }
      const content = lines.join("\n").trimEnd();
      if (content.trim().length === 0) {
        throw new Error(`${sourcePath}:${lineNumber}: empty tutorial region ${active}`);
      }
      regions.set(active, content);
      active = null;
      lines = [];
    } else if (active !== null) {
      lines.push(line);
    }

    if (conditional) {
      if (["if", "ifdef", "ifndef"].includes(conditional[1])) {
        conditionalDepth += 1;
      } else if (conditional[1] === "endif") {
        conditionalDepth -= 1;
        if (conditionalDepth < 0) {
          throw new Error(`${sourcePath}:${lineNumber}: unmatched conditional end`);
        }
      }
    }
  }

  if (active !== null) {
    throw new Error(`${sourcePath}: unterminated tutorial region ${active}`);
  }
  return regions;
}

export function renderTutorialSnippets(
  body,
  { filePath, tutorialDirectory = defaultTutorialDirectory },
) {
  const stem = basename(filePath, ".md");
  if (!/^tutorial-[0-9]{3}$/.test(stem)) {
    throw new Error(`${filePath}: tutorial snippet preprocessing requires a tutorial-NNN page`);
  }
  inspectMarkdownFences(body, filePath);

  const directives = [];
  for (const [index, line] of body.split("\n").entries()) {
    if (!line.includes("rift:snippet")) {
      continue;
    }
    const match = line.match(directivePattern);
    if (!match) {
      const kind = line.trim().startsWith("<!-- rift:snippet") ? "malformed" : "not the sole content";
      throw new Error(`${filePath}:${index + 1}: ${kind} tutorial snippet directive; it must be the sole content of its line`);
    }
    if (!match[1].startsWith(`${stem}.`)) {
      throw new Error(`${filePath}:${index + 1}: ${match[1]} belongs to ${stem}`);
    }
    if (directives.includes(match[1])) {
      throw new Error(`${filePath}:${index + 1}: tutorial region ${match[1]} is referenced more than once`);
    }
    directives.push(match[1]);
  }

  const sourcePath = resolve(tutorialDirectory, `${stem}.cpp`);
  if (sourcePath !== join(resolve(tutorialDirectory), `${stem}.cpp`)) {
    throw new Error(`${filePath}: tutorial source path escapes the tutorial directory`);
  }
  const regions = extractTutorialRegions(readFileSync(sourcePath, "utf8"), sourcePath);
  for (const directive of directives) {
    if (!regions.has(directive)) {
      throw new Error(`${filePath}: unknown region ${directive}`);
    }
  }
  const unused = [...regions.keys()].filter((name) => !directives.includes(name));
  if (unused.length !== 0) {
    throw new Error(`${filePath}: source region is not referenced: ${unused.join(", ")}`);
  }

  return body
    .split("\n")
    .map((line) => {
      const match = line.match(directivePattern);
      if (!match) {
        return line;
      }
      const code = regions.get(match[1]);
      if (code.includes("```")) {
        throw new Error(`${sourcePath}: region ${match[1]} contains a backtick fence`);
      }
      return `\`\`\`cpp\n${code}\n\`\`\``;
    })
    .join("\n");
}

export function withTutorialSnippets(
  sourceAdapter,
  { tutorialDirectory = defaultTutorialDirectory } = {},
) {
  return {
    name: "rift-tutorial-markdown",
    async resolve(context) {
      const resolved = await sourceAdapter.resolve(context);
      for (const group of resolved.groups ?? []) {
        for (const page of group.pages ?? []) {
          if (/tutorial-[0-9]{3}\.md$/.test(page.file)) {
            page.preprocess = [
              ...(page.preprocess ?? []),
              (body, preprocessContext) =>
                renderTutorialSnippets(body, {
                  ...preprocessContext,
                  tutorialDirectory,
                }),
            ];
          }
        }
      }
      const tutorialSources = readdirSync(tutorialDirectory)
        .filter((name) => /^tutorial-[0-9]{3}\.cpp$/.test(name))
        .map((name) => join(tutorialDirectory, name));
      return {
        ...resolved,
        watchPaths: [...(resolved.watchPaths ?? []), ...tutorialSources],
      };
    },
  };
}
