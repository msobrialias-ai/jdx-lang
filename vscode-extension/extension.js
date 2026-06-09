const vscode = require('vscode');

const KEYWORDS = [
  'let', 'const', 'fname', 'return', 'if', 'elif', 'else', 'while', 'for',
  'break', 'continue', 'class', 'try', 'catch', 'throw', 'this', 'import',
  'export', 'default', 'from', 'as', 'true', 'false', 'null'
];

const BUILTIN_SYMBOLS = [
  { label: 'System', kind: vscode.CompletionItemKind.Module, detail: 'Root runtime namespace' },
  { label: 'Develoment', kind: vscode.CompletionItemKind.Module, detail: 'Development namespace' }
];

const SYSTEM_MEMBERS = {
  System: [
    ['Args', 'Program arguments array'],
    ['Output', 'Write values to stdout'],
    ['Log', 'Log a message'],
    ['Warn', 'Log a warning'],
    ['Error', 'Log an error'],
    ['ReadFile', 'Read a file as text'],
    ['WriteFile', 'Write text to a file'],
    ['Exists', 'Check whether a path exists'],
    ['Clock', 'Current UTC time string'],
    ['Time', 'Milliseconds since epoch'],
    ['Sleep', 'Sleep for N milliseconds'],
    ['Random', 'Random 64-bit integer'],
    ['Type', 'Return the runtime type name'],
    ['Len', 'Length of a string, array, or object'],
    ['Lower', 'Lowercase a string'],
    ['Upper', 'Uppercase a string'],
    ['SocketError', 'Current socket error string'],
    ['FileSystem', 'Filesystem information object'],
    ['ShowSystemInfo', 'Platform/compiler information object'],
    ['JGex', 'Create a regex-like matcher'],
    ['Regex', 'Alias of JGex'],
    ['Server', 'Networking namespace'],
    ['SafeExec', 'Execute a callable and capture failures']
  ],
  'System.FileSystem': [
    ['cwd', 'Current working directory'],
    ['home', 'Home directory'],
    ['temp', 'Temporary directory'],
    ['root', 'Root directory']
  ],
  'System.Info': [
    ['platform', 'Platform name'],
    ['architecture', 'CPU architecture'],
    ['compiler', 'Compiler name'],
    ['endianness', 'Endianness']
  ],
  'System.JGex': [
    ['pattern', 'Original pattern'],
    ['test', 'Full match test'],
    ['match', 'Prefix match'],
    ['search', 'Search inside text'],
    ['replace', 'Replace all matches'],
    ['split', 'Split by matches']
  ],
  'System.SafeExec.Result': [
    ['ok', 'true when the call succeeded'],
    ['error', 'Error message or null'],
    ['value', 'Returned value or null']
  ],
  'System.Server': [
    ['Socket', 'Create a socket object'],
    ['Resolver', 'Resolve hostnames'],
    ['Connect', 'Create a connected socket'],
    ['Listen', 'Create a listening socket'],
    ['JsonParse', 'Parse JSON text'],
    ['JsonStringify', 'Stringify JSON'],
    ['JsonStringfy', 'Legacy alias with original spelling'],
    ['ResponseHeader', 'Build an HTTP response header']
  ],
  'Develoment': [
    ['Stacktrace', 'Stacktrace configuration object'],
    ['Test', 'Development test helpers']
  ],
  'Develoment.Stacktrace': [
    ['Level', 'Stacktrace level'],
    ['Type', 'Stacktrace style']
  ],
  'Develoment.Test': [
    ['Assert', 'Assert truthiness'],
    ['Equal', 'Assert equality'],
    ['Throws', 'Assert that a callable throws']
  ]
};

const KEYWORD_DOCS = new Map([
  ['let', 'Mutable binding declaration. `let name = value;`'],
  ['const', 'Immutable binding declaration. Constants require an initializer.'],
  ['fname', 'Function declaration keyword.'],
  ['class', 'Class declaration keyword.'],
  ['if', 'Conditional branch.'],
  ['elif', 'Additional conditional branch.'],
  ['else', 'Fallback branch.'],
  ['while', 'Loop while the condition is truthy.'],
  ['for', 'C-style for loop.'],
  ['break', 'Exit the nearest loop.'],
  ['continue', 'Skip to the next loop iteration.'],
  ['try', 'Start a try block.'],
  ['catch', 'Catch an exception.'],
  ['throw', 'Throw a runtime error value.'],
  ['import', 'Import a module or bindings.'],
  ['export', 'Export a declaration or binding list.'],
  ['default', 'Default export modifier.'],
  ['from', 'Source clause in an import statement.'],
  ['as', 'Rename binding in import/export clauses.'],
  ['this', 'Current object receiver.'],
  ['true', 'Boolean literal.'],
  ['false', 'Boolean literal.'],
  ['null', 'Null literal.']
]);

function severityFromSetting(value) {
  switch (value) {
    case 'error':
      return vscode.DiagnosticSeverity.Error;
    case 'information':
      return vscode.DiagnosticSeverity.Information;
    case 'hint':
      return vscode.DiagnosticSeverity.Hint;
    case 'warning':
    default:
      return vscode.DiagnosticSeverity.Warning;
  }
}

function isIdentifierStart(ch) {
  return /[A-Za-z_]/.test(ch);
}

function isIdentifierPart(ch) {
  return /[A-Za-z0-9_]/.test(ch);
}

function lineRange(line, startCol, endCol) {
  return new vscode.Range(new vscode.Position(line, startCol), new vscode.Position(line, endCol));
}

function stripLineComment(line) {
  let inSingle = false;
  let inDouble = false;
  let escaped = false;
  for (let i = 0; i < line.length - 1; i++) {
    const ch = line[i];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (ch === '\\') {
      escaped = true;
      continue;
    }
    if (!inDouble && ch === '\'') {
      inSingle = !inSingle;
      continue;
    }
    if (!inSingle && ch === '"') {
      inDouble = !inDouble;
      continue;
    }
    if (!inSingle && !inDouble && ch === '/' && line[i + 1] === '/') {
      return line.slice(0, i);
    }
  }
  return line;
}

function containsUnbalancedParens(line) {
  let balance = 0;
  let inSingle = false;
  let inDouble = false;
  let escaped = false;
  for (let i = 0; i < line.length; i++) {
    const ch = line[i];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (ch === '\\') {
      escaped = true;
      continue;
    }
    if (!inDouble && ch === '\'') {
      inSingle = !inSingle;
      continue;
    }
    if (!inSingle && ch === '"') {
      inDouble = !inDouble;
      continue;
    }
    if (inSingle || inDouble) {
      continue;
    }
    if (ch === '(') balance++;
    if (ch === ')') balance--;
  }
  return balance !== 0;
}

function collectLocalSymbols(text) {
  const symbols = new Map();

  const add = (name, kind, detail) => {
    if (!name || symbols.has(name)) return;
    symbols.set(name, { name, kind, detail });
  };

  const bindingRe = /\b(let|const|fname|class)\s+([A-Za-z_][A-Za-z0-9_]*)/g;
  let match;
  while ((match = bindingRe.exec(text))) {
    const kind = match[1] === 'class' ? vscode.CompletionItemKind.Class
      : match[1] === 'fname' ? vscode.CompletionItemKind.Function
      : vscode.CompletionItemKind.Variable;
    add(match[2], kind, `${match[1]} declared in this file`);
  }

  const exportListRe = /\bexport\s*\{([^}]*)\}/g;
  while ((match = exportListRe.exec(text))) {
    const parts = match[1].split(',');
    for (const raw of parts) {
      const seg = raw.trim();
      const m = seg.match(/^([A-Za-z_][A-Za-z0-9_]*)(?:\s+as\s+([A-Za-z_][A-Za-z0-9_]*))?$/);
      if (m) {
        add(m[2] || m[1], vscode.CompletionItemKind.Variable, 'Exported binding');
      }
    }
  }

  const importRe = /\bimport\s+(?:([A-Za-z_][A-Za-z0-9_]*)\s*,\s*)?(?:\{([^}]*)\}|\*\s+as\s+([A-Za-z_][A-Za-z0-9_]*))\s+from\s+["'][^"']+["']/g;
  while ((match = importRe.exec(text))) {
    if (match[1]) add(match[1], vscode.CompletionItemKind.Variable, 'Default import');
    if (match[3]) add(match[3], vscode.CompletionItemKind.Variable, 'Namespace import');
    if (match[2]) {
      for (const raw of match[2].split(',')) {
        const seg = raw.trim();
        const m = seg.match(/^([A-Za-z_][A-Za-z0-9_]*)(?:\s+as\s+([A-Za-z_][A-Za-z0-9_]*))?$/);
        if (m) {
          add(m[2] || m[1], vscode.CompletionItemKind.Variable, 'Imported binding');
        }
      }
    }
  }

  return [...symbols.values()];
}

function makeCompletionItem(name, kind, detail, documentation) {
  const item = new vscode.CompletionItem(name, kind);
  item.detail = detail || '';
  if (documentation) {
    item.documentation = new vscode.MarkdownString(documentation);
  }
  return item;
}

function buildSystemCompletions(namespace, prefix) {
  const items = [];
  for (const [name, doc] of SYSTEM_MEMBERS[namespace] || []) {
    if (prefix && !name.toLowerCase().startsWith(prefix.toLowerCase())) {
      continue;
    }
    const item = new vscode.CompletionItem(name, vscode.CompletionItemKind.Property);
    item.detail = namespace;
    item.documentation = new vscode.MarkdownString(`**${namespace}.${name}**\n\n${doc}`);
    items.push(item);
  }
  return items;
}

function buildKeywordCompletions(prefix) {
  return KEYWORDS
    .filter((kw) => !prefix || kw.startsWith(prefix))
    .map((kw) => makeCompletionItem(kw, vscode.CompletionItemKind.Keyword, 'JDX keyword', KEYWORD_DOCS.get(kw)));
}

function buildSnippetCompletions() {
  const snippets = [
    ['fname', 'fname ${1:name}(${2:params}) {\n\t${0}\n}', 'Function template'],
    ['class', 'class ${1:Name} {\n\t${0}\n}', 'Class template'],
    ['if', 'if (${1:condition}) {\n\t${2}\n}', 'If template'],
    ['for', 'for (${1:init}; ${2:condition}; ${3:increment}) {\n\t${0}\n}', 'For loop template'],
    ['while', 'while (${1:condition}) {\n\t${0}\n}', 'While loop template'],
    ['try', 'try {\n\t${1}\n} catch (${2:error}) {\n\t${0}\n}', 'Try/catch template'],
    ['import', 'import { ${1:name} } from "${2:jdx:module}";', 'Named import'],
    ['export', 'export { ${1:local} as ${2:exported} };', 'Export binding']
  ];

  return snippets.map(([label, body, detail]) => {
    const item = new vscode.CompletionItem(label, vscode.CompletionItemKind.Snippet);
    item.insertText = new vscode.SnippetString(body);
    item.detail = detail;
    return item;
  });
}

function inferNamespacePrefix(textBeforeCursor) {
  const m = textBeforeCursor.match(/\b(System|Develoment)(?:\.(\w*))?$/);
  if (!m) return null;
  return { namespace: m[1], prefix: m[2] || '' };
}

function detectMissingSemicolon(trimmed) {
  if (!trimmed) return false;
  if (/^(?:\/\/|\/\*|\*|#)/.test(trimmed)) return false;
  if (/[{;}]$/.test(trimmed)) return false;
  if (containsUnbalancedParens(trimmed)) return false;
  if (/^(?:if|elif|else|while|for|try|catch|class|fname)\b/.test(trimmed)) return false;
  return /^(?:let|const|return|throw|import|export)\b/.test(trimmed) || /^[A-Za-z_][A-Za-z0-9_]*\s*[=.(]/.test(trimmed);
}

function lintDocument(document) {
  const settings = vscode.workspace.getConfiguration('jdx');
  if (!settings.get('linting.enabled', true)) {
    return [];
  }

  const severity = severityFromSetting(settings.get('linting.severity', 'warning'));
  const diagnostics = [];
  const text = document.getText();
  const lines = text.split(/\r?\n/);

  // Structural scan: braces, brackets, parens, strings and block comments.
  const stack = [];
  let inBlockComment = false;
  let inString = null;
  let escaped = false;
  let stringStart = null;
  let blockStart = null;

  for (let lineIndex = 0; lineIndex < lines.length; lineIndex++) {
    const line = lines[lineIndex];
    for (let col = 0; col < line.length; col++) {
      const ch = line[col];
      const next = col + 1 < line.length ? line[col + 1] : '';

      if (inString) {
        if (escaped) {
          escaped = false;
          continue;
        }
        if (ch === '\\') {
          escaped = true;
          continue;
        }
        if (ch === inString) {
          inString = null;
          stringStart = null;
        }
        continue;
      }

      if (inBlockComment) {
        if (ch === '*' && next === '/') {
          inBlockComment = false;
          col++;
          blockStart = null;
        }
        continue;
      }

      if (ch === '/' && next === '/') {
        break;
      }
      if (ch === '/' && next === '*') {
        inBlockComment = true;
        blockStart = new vscode.Position(lineIndex, col);
        col++;
        continue;
      }
      if (ch === '"' || ch === '\'') {
        inString = ch;
        stringStart = new vscode.Position(lineIndex, col);
        continue;
      }

      if ('([{'.includes(ch)) {
        stack.push({ ch, pos: new vscode.Position(lineIndex, col) });
        continue;
      }
      if (')]}'.includes(ch)) {
        const expected = { ')': '(', ']': '[', '}': '{' }[ch];
        const last = stack.pop();
        if (!last || last.ch !== expected) {
          diagnostics.push(new vscode.Diagnostic(
            lineRange(lineIndex, col, col + 1),
            `Unexpected closing '${ch}'.`,
            vscode.DiagnosticSeverity.Error
          ));
        }
      }
    }

    const stripped = stripLineComment(line);
    const trimmed = stripped.trim();

    if (!trimmed) continue;

    // Missing initializer on const.
    const constMatch = trimmed.match(/^(?:export\s+)?(?:default\s+)?const\s+([A-Za-z_][A-Za-z0-9_]*)\s*(.*)$/);
    if (constMatch) {
      const rest = constMatch[2] || '';
      if (!/=/.test(rest)) {
        const start = stripped.indexOf('const');
        diagnostics.push(new vscode.Diagnostic(
          lineRange(lineIndex, Math.max(0, start), Math.min(line.length, start + 5)),
          'Constants require an initializer.',
          vscode.DiagnosticSeverity.Error
        ));
      }
    }

    // Declaration shape checks.
    if (/^(?:export\s+)?(?:default\s+)?fname\b/.test(trimmed)) {
      const hasName = /^(?:export\s+)?(?:default\s+)?fname\s+[A-Za-z_][A-Za-z0-9_]*\s*\(/.test(trimmed);
      const defaultAnon = /^(?:export\s+)?default\s+fname\s*\(/.test(trimmed);
      if (!hasName && !defaultAnon) {
        diagnostics.push(new vscode.Diagnostic(
          lineRange(lineIndex, 0, Math.min(line.length, trimmed.length)),
          'Function declarations use `fname name(...) { ... }`.',
          vscode.DiagnosticSeverity.Error
        ));
      }
      continue;
    }

    if (/^(?:export\s+)?(?:default\s+)?class\b/.test(trimmed)) {
      const hasName = /^(?:export\s+)?(?:default\s+)?class\s+[A-Za-z_][A-Za-z0-9_]*\s*\{/.test(trimmed);
      const defaultAnon = /^(?:export\s+)?default\s+class\s*\{/.test(trimmed);
      if (!hasName && !defaultAnon) {
        diagnostics.push(new vscode.Diagnostic(
          lineRange(lineIndex, 0, Math.min(line.length, trimmed.length)),
          'Class declarations use `class Name { ... }`.',
          vscode.DiagnosticSeverity.Error
        ));
      }
      continue;
    }

    // Import/export structure.
    if (/^import\b/.test(trimmed) && !/;\s*$/.test(trimmed)) {
      diagnostics.push(new vscode.Diagnostic(
        lineRange(lineIndex, 0, line.length),
        'Import statements must end with a semicolon.',
        severity
      ));
      continue;
    }
    if (/^export\b/.test(trimmed) && !/(?:\{.*\}|let\b|const\b|fname\b|class\b|default\b)/.test(trimmed)) {
      diagnostics.push(new vscode.Diagnostic(
        lineRange(lineIndex, 0, line.length),
        'Export statements should export a declaration, a default expression, or an export list.',
        severity
      ));
    }

    // Generic semicolon check for statement-like lines.
    if (detectMissingSemicolon(trimmed)) {
      diagnostics.push(new vscode.Diagnostic(
        lineRange(lineIndex, 0, line.length),
        'Possible missing semicolon.',
        severity
      ));
    }

    // Catch clause check.
    if (/^catch\b/.test(trimmed) && !/^catch\s*\(\s*[A-Za-z_][A-Za-z0-9_]*\s*\)\s*\{?/.test(trimmed)) {
      diagnostics.push(new vscode.Diagnostic(
        lineRange(lineIndex, 0, line.length),
        "Catch clauses use `catch (name) { ... }`.",
        vscode.DiagnosticSeverity.Error
      ));
    }
  }

  if (inString && stringStart) {
    diagnostics.push(new vscode.Diagnostic(
      new vscode.Range(stringStart, stringStart),
      'Unterminated string literal.',
      vscode.DiagnosticSeverity.Error
    ));
  }
  if (inBlockComment && blockStart) {
    diagnostics.push(new vscode.Diagnostic(
      new vscode.Range(blockStart, blockStart),
      'Unterminated block comment.',
      vscode.DiagnosticSeverity.Error
    ));
  }
  while (stack.length > 0) {
    const last = stack.pop();
    diagnostics.push(new vscode.Diagnostic(
      new vscode.Range(last.pos, last.pos.translate(0, 1)),
      `Unclosed '${last.ch}'.`,
      vscode.DiagnosticSeverity.Error
    ));
  }

  return diagnostics;
}

function getHoverText(word) {
  if (KEYWORD_DOCS.has(word)) {
    return `**${word}**\n\n${KEYWORD_DOCS.get(word)}`;
  }
  for (const [namespace, members] of Object.entries(SYSTEM_MEMBERS)) {
    if (namespace === word) {
      return `**${namespace}**\n\nRuntime namespace.`;
    }
    const member = members.find(([name]) => name === word);
    if (member) {
      return `**${namespace}.${member[0]}**\n\n${member[1]}`;
    }
  }
  return null;
}

function activate(context) {
  const diagnostics = vscode.languages.createDiagnosticCollection('jdx');
  context.subscriptions.push(diagnostics);

  const refresh = (document) => {
    if (document && document.languageId === 'jdx') {
      diagnostics.set(document.uri, lintDocument(document));
    }
  };

  context.subscriptions.push(
    vscode.workspace.onDidOpenTextDocument(refresh),
    vscode.workspace.onDidChangeTextDocument((event) => refresh(event.document)),
    vscode.workspace.onDidCloseTextDocument((document) => diagnostics.delete(document.uri))
  );

  if (vscode.window.activeTextEditor?.document.languageId === 'jdx') {
    refresh(vscode.window.activeTextEditor.document);
  }

  const completionProvider = vscode.languages.registerCompletionItemProvider(
    'jdx',
    {
      provideCompletionItems(document, position) {
        const cfg = vscode.workspace.getConfiguration('jdx');
        const text = document.getText();
        const linePrefix = document.lineAt(position.line).text.slice(0, position.character);

        const ns = inferNamespacePrefix(linePrefix);
        if (ns && cfg.get('completion.showSystemMembers', true)) {
          return buildSystemCompletions(ns.namespace === 'System' ? 'System' : ns.namespace, ns.prefix);
        }

        const completions = [];
        completions.push(...buildKeywordCompletions(linePrefix.match(/[A-Za-z_][A-Za-z0-9_]*$/)?.[0] || ''));
        completions.push(...buildSnippetCompletions());

        if (cfg.get('completion.showSystemMembers', true)) {
          completions.push(...buildSystemCompletions('System', ''));
          completions.push(...buildSystemCompletions('System.FileSystem', ''));
          completions.push(...buildSystemCompletions('System.Info', ''));
          completions.push(...buildSystemCompletions('System.JGex', ''));
          completions.push(...buildSystemCompletions('System.Server', ''));
          completions.push(...buildSystemCompletions('Develoment', ''));
          completions.push(...buildSystemCompletions('Develoment.Stacktrace', ''));
          completions.push(...buildSystemCompletions('Develoment.Test', ''));
        }

        if (cfg.get('completion.showLocalSymbols', true)) {
          for (const sym of collectLocalSymbols(text)) {
            const item = new vscode.CompletionItem(sym.name, sym.kind);
            item.detail = sym.detail;
            completions.push(item);
          }
        }

        completions.push(...BUILTIN_SYMBOLS.map((item) => makeCompletionItem(item.label, item.kind, item.detail)));
        return completions;
      }
    },
    '.', ' ', '\t'
  );
  context.subscriptions.push(completionProvider);

  const hoverProvider = vscode.languages.registerHoverProvider('jdx', {
    provideHover(document, position) {
      const range = document.getWordRangeAtPosition(position, /[A-Za-z_][A-Za-z0-9_]*/);
      if (!range) return;
      const word = document.getText(range);
      const text = getHoverText(word);
      if (!text) return;
      return new vscode.Hover(new vscode.MarkdownString(text));
    }
  });
  context.subscriptions.push(hoverProvider);

  const lintNow = vscode.commands.registerCommand('jdx.lintNow', () => {
    const editor = vscode.window.activeTextEditor;
    if (editor?.document.languageId === 'jdx') {
      diagnostics.set(editor.document.uri, lintDocument(editor.document));
    }
  });
  context.subscriptions.push(lintNow);
}

function deactivate() {}

module.exports = { activate, deactivate };
