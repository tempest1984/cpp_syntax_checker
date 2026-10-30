/**
 * C++ Syntax Parser Studio - Frontend Application Logic
 */

// Application State
const state = {
  editorType: 'monaco', // 'monaco' or 'fallback'
  monacoEditor: null,
  currentFile: 'samples/errors.cpp',
  decorations: [],
  lastResult: null,
  isDarkTheme: true,
  samples: []
};

// DOM Elements
const DOM = {
  sampleSelect: document.getElementById('sampleSelect'),
  uploadBtn: document.getElementById('uploadBtn'),
  fileInput: document.getElementById('fileInput'),
  runBtn: document.getElementById('runBtn'),
  clearBtn: document.getElementById('clearBtn'),
  themeToggleBtn: document.getElementById('themeToggleBtn'),
  moonIcon: document.getElementById('moonIcon'),
  sunIcon: document.getElementById('sunIcon'),
  
  currentFileName: document.getElementById('currentFileName'),
  fileBadge: document.getElementById('fileBadge'),
  verboseToggle: document.getElementById('verboseToggle'),
  undeclaredToggle: document.getElementById('undeclaredToggle'),
  copyCodeBtn: document.getElementById('copyCodeBtn'),
  downloadCodeBtn: document.getElementById('downloadCodeBtn'),

  editorDropZone: document.getElementById('editorDropZone'),
  dropOverlay: document.getElementById('dropOverlay'),
  monacoContainer: document.getElementById('monacoEditor'),
  fallbackEditor: document.getElementById('fallbackEditor'),
  fallbackTextarea: document.getElementById('fallbackTextarea'),
  fallbackLineNumbers: document.getElementById('fallbackLineNumbers'),

  cursorPos: document.getElementById('cursorPos'),
  charCount: document.getElementById('charCount'),
  lineCount: document.getElementById('lineCount'),

  tabButtons: document.querySelectorAll('.results-tabs .tab-btn'),
  tabPanes: document.querySelectorAll('.tab-pane'),
  errorBadgeCount: document.getElementById('errorBadgeCount'),
  executionTime: document.getElementById('executionTime'),

  statusBanner: document.getElementById('statusBanner'),
  bannerIcon: document.getElementById('bannerIcon'),
  bannerTitle: document.getElementById('bannerTitle'),
  bannerSubtitle: document.getElementById('bannerSubtitle'),

  errorList: document.getElementById('errorList'),
  rawOutput: document.getElementById('rawOutput'),
  astOutput: document.getElementById('astOutput'),
  tokensOutput: document.getElementById('tokensOutput'),

  copyRawBtn: document.getElementById('copyRawBtn'),
  copyAstBtn: document.getElementById('copyAstBtn'),
  copyTokensBtn: document.getElementById('copyTokensBtn'),

  serverStatusDot: document.getElementById('serverStatusDot'),
  serverStatusText: document.getElementById('serverStatusText')
};

// ==========================================================================
// Editor Abstraction Layer (Monaco + Robust Fallback)
// ==========================================================================

function getEditorCode() {
  if (state.editorType === 'monaco' && state.monacoEditor) {
    return state.monacoEditor.getValue();
  }
  return DOM.fallbackTextarea.value;
}

function setEditorCode(code) {
  if (state.editorType === 'monaco' && state.monacoEditor) {
    state.monacoEditor.setValue(code);
    state.monacoEditor.setPosition({ lineNumber: 1, column: 1 });
  } else {
    DOM.fallbackTextarea.value = code;
    updateFallbackLineNumbers();
  }
  updateStats();
  clearErrorMarkers();
}

function jumpToLine(lineNumber, column = 1) {
  if (state.editorType === 'monaco' && state.monacoEditor) {
    state.monacoEditor.revealLineInCenter(lineNumber);
    state.monacoEditor.setPosition({ lineNumber: lineNumber, column: column || 1 });
    state.monacoEditor.focus();

    // Temporary highlight flash on jump
    const flashDecoration = [{
      range: new monaco.Range(lineNumber, 1, lineNumber, 1),
      options: { isWholeLine: true, className: 'monaco-line-flash' }
    }];
    const flashId = state.monacoEditor.deltaDecorations([], flashDecoration);
    setTimeout(() => {
      if (state.monacoEditor) {
        state.monacoEditor.deltaDecorations(flashId, []);
      }
    }, 1500);

  } else {
    DOM.fallbackTextarea.focus();
    // Calculate caret position in textarea
    const lines = DOM.fallbackTextarea.value.split('\n');
    let pos = 0;
    for (let i = 0; i < lineNumber - 1 && i < lines.length; i++) {
      pos += lines[i].length + 1;
    }
    if (column > 1) pos += (column - 1);
    DOM.fallbackTextarea.setSelectionRange(pos, pos);
    
    // Scroll fallback line into view
    const lineEl = document.getElementById(`fallback-line-${lineNumber}`);
    if (lineEl) lineEl.scrollIntoView({ behavior: 'smooth', block: 'center' });
  }
}

function updateStats() {
  const code = getEditorCode();
  const chars = code.length;
  const lines = code.split('\n').length;
  
  DOM.charCount.textContent = `${chars.toLocaleString()} chars`;
  DOM.lineCount.textContent = `${lines.toLocaleString()} lines`;
}

function updateFallbackLineNumbers() {
  const code = DOM.fallbackTextarea.value;
  const count = code.split('\n').length;
  let html = '';
  for (let i = 1; i <= count; i++) {
    html += `<div id="fallback-line-${i}" class="line-no">${i}</div>`;
  }
  DOM.fallbackLineNumbers.innerHTML = html;
}

// Fallback textarea event wiring
DOM.fallbackTextarea.addEventListener('input', () => {
  updateFallbackLineNumbers();
  updateStats();
  clearErrorMarkers();
});

DOM.fallbackTextarea.addEventListener('scroll', () => {
  DOM.fallbackLineNumbers.scrollTop = DOM.fallbackTextarea.scrollTop;
});

DOM.fallbackTextarea.addEventListener('keyup', (e) => {
  const selStart = DOM.fallbackTextarea.selectionStart;
  const code = DOM.fallbackTextarea.value.substring(0, selStart);
  const lines = code.split('\n');
  const line = lines.length;
  const col = lines[lines.length - 1].length + 1;
  DOM.cursorPos.textContent = `Ln ${line}, Col ${col}`;
});

// Setup Monaco Editor
function initMonacoEditor() {
  if (typeof require !== 'undefined') {
    require.config({ paths: { vs: 'https://cdnjs.cloudflare.com/ajax/libs/monaco-editor/0.45.0/min/vs' } });
    
    // Safety timeout in case CDN fails to load
    const monacoTimeout = setTimeout(() => {
      if (!state.monacoEditor) {
        enableFallbackEditor();
      }
    }, 3000);

    require(['vs/editor/editor.main'], function () {
      clearTimeout(monacoTimeout);
      try {
        state.monacoEditor = monaco.editor.create(DOM.monacoContainer, {
          value: '',
          language: 'cpp',
          theme: state.isDarkTheme ? 'vs-dark' : 'vs',
          automaticLayout: true,
          fontFamily: 'Fira Code, Consolas, monospace',
          fontSize: 13.5,
          lineHeight: 22,
          minimap: { enabled: true },
          scrollBeyondLastLine: false,
          roundedSelection: false,
          glyphMargin: true,
          tabSize: 4,
          insertSpaces: true,
          folding: true,
          bracketPairColorization: { enabled: true }
        });

        state.editorType = 'monaco';
        DOM.fallbackEditor.style.display = 'none';
        DOM.monacoContainer.style.display = 'block';

        // Listen for cursor position changes
        state.monacoEditor.onDidChangeCursorPosition((e) => {
          DOM.cursorPos.textContent = `Ln ${e.position.lineNumber}, Col ${e.position.column}`;
        });

        // Listen for content changes
        state.monacoEditor.onDidChangeModelContent(() => {
          updateStats();
          clearErrorMarkers();
        });

        // Run default sample once ready
        loadInitialSample();

      } catch (err) {
        console.warn('Monaco initialization failed, falling back:', err);
        enableFallbackEditor();
      }
    });
  } else {
    enableFallbackEditor();
  }
}

function enableFallbackEditor() {
  state.editorType = 'fallback';
  DOM.monacoContainer.style.display = 'none';
  DOM.fallbackEditor.style.display = 'flex';
  loadInitialSample();
}

// ==========================================================================
// Error Visualization & Markers
// ==========================================================================

function clearErrorMarkers() {
  if (state.editorType === 'monaco' && state.monacoEditor) {
    const model = state.monacoEditor.getModel();
    if (model) {
      monaco.editor.setModelMarkers(model, 'cpp-parser', []);
    }
    state.decorations = state.monacoEditor.deltaDecorations(state.decorations, []);
  } else {
    document.querySelectorAll('.fallback-linenumbers .line-no').forEach(el => {
      el.classList.remove('has-error');
    });
  }
}

function applyErrorMarkers(errors) {
  clearErrorMarkers();
  if (!errors || errors.length === 0) return;

  if (state.editorType === 'monaco' && state.monacoEditor) {
    const model = state.monacoEditor.getModel();
    if (!model) return;

    // Set Monaco squiggly line markers
    const markers = errors.map(err => {
      const line = err.line;
      const col = err.col || 1;
      const lineLength = model.getLineLength(line) || 80;

      return {
        severity: monaco.MarkerSeverity.Error,
        message: err.message || `Error at line ${line}`,
        startLineNumber: line,
        startColumn: col,
        endLineNumber: line,
        endColumn: col > 1 ? col + 1 : lineLength + 1
      };
    });
    monaco.editor.setModelMarkers(model, 'cpp-parser', markers);

    // Apply gutter error glyph and line tint
    const newDecorations = errors.map(err => ({
      range: new monaco.Range(err.line, 1, err.line, 1),
      options: {
        isWholeLine: true,
        className: 'monaco-line-error',
        glyphMarginClassName: 'monaco-glyph-error',
        glyphMarginHoverMessage: { value: `**Error at line ${err.line}**: ${err.message}` }
      }
    }));
    state.decorations = state.monacoEditor.deltaDecorations([], newDecorations);

  } else {
    // Apply markers to fallback line numbers
    errors.forEach(err => {
      const lineEl = document.getElementById(`fallback-line-${err.line}`);
      if (lineEl) {
        lineEl.classList.add('has-error');
        lineEl.title = err.message || `Error at line ${err.line}`;
      }
    });
  }
}

// ==========================================================================
// Parsing & Execution
// ==========================================================================

async function runParser() {
  const code = getEditorCode();
  
  // UI Loading State
  DOM.runBtn.disabled = true;
  DOM.runBtn.querySelector('.btn-text').textContent = 'Parsing...';
  
  const options = {
    code: code,
    verbose: DOM.verboseToggle.checked,
    noUndeclared: !DOM.undeclaredToggle.checked,
    ast: true,
    tokens: true
  };

  try {
    const response = await fetch('/api/parse', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(options)
    });

    if (!response.ok) {
      throw new Error(`Server returned HTTP ${response.status}`);
    }

    const data = await response.json();
    state.lastResult = data;
    renderResults(data);

  } catch (err) {
    console.error('Parse error:', err);
    renderErrorState(err.message);
  } finally {
    DOM.runBtn.disabled = false;
    DOM.runBtn.querySelector('.btn-text').textContent = 'Run Parser';
  }
}

function renderResults(data) {
  // Update Benchmark Timing
  DOM.executionTime.textContent = `${data.durationMs} ms`;

  // Update Error Badge Count
  DOM.errorBadgeCount.textContent = data.errorCount;
  DOM.errorBadgeCount.style.display = data.errorCount > 0 ? 'inline-block' : 'none';

  // Raw CLI Output
  DOM.rawOutput.textContent = data.rawOutput || '// No output returned';

  // AST Tab
  if (data.ast) {
    DOM.astOutput.textContent = data.ast;
  } else if (data.errorCount > 0) {
    DOM.astOutput.textContent = `// Abstract Syntax Tree is only generated when code has no errors.\n// Found ${data.errorCount} error(s). Please fix the syntax errors first.`;
  } else {
    DOM.astOutput.textContent = '// No AST available for empty input.';
  }

  // Tokens Tab
  DOM.tokensOutput.textContent = data.tokens || '// No tokens generated';

  // Update Status Banner
  if (data.success && data.errorCount === 0) {
    DOM.statusBanner.className = 'status-banner banner-success';
    DOM.bannerIcon.textContent = '✅';
    DOM.bannerTitle.textContent = 'No errors found';
    DOM.bannerSubtitle.textContent = 'C++ syntax is 100% valid! Translation unit passed all grammar and type checks.';
    
    // Empty error list state
    DOM.errorList.innerHTML = `
      <div class="empty-state">
        <div class="empty-icon">🎉</div>
        <h4>All Syntax Checks Passed</h4>
        <p>No syntax errors or undeclared types detected. You can inspect the generated <strong>Syntax Tree (AST)</strong> in the tab above.</p>
      </div>
    `;
    clearErrorMarkers();

  } else {
    DOM.statusBanner.className = 'status-banner banner-error';
    DOM.bannerIcon.textContent = '❌';
    DOM.bannerTitle.textContent = data.summary || `${data.errorCount} errors found`;
    DOM.bannerSubtitle.textContent = 'Syntax errors detected. Click any error below to jump directly to that line.';

    renderErrorCards(data.errors);
    applyErrorMarkers(data.errors);

    // Switch to Errors tab
    switchTab('errorsTab');
  }
}

function renderErrorCards(errors) {
  if (!errors || errors.length === 0) {
    DOM.errorList.innerHTML = '';
    return;
  }

  const codeLines = getEditorCode().split('\n');

  let html = '';
  errors.forEach((err, idx) => {
    const lineIndex = err.line - 1;
    const previewLine = (lineIndex >= 0 && lineIndex < codeLines.length) 
      ? codeLines[lineIndex].trim() 
      : '';

    html += `
      <div class="error-card" onclick="jumpToLine(${err.line}, ${err.col || 1})" title="Click to jump to line ${err.line}">
        <div class="error-card-header">
          <div class="error-title-wrap">
            <span class="error-tag">${escapeHtml(err.title || `Error at line ${err.line}`)}</span>
            ${err.col ? `<span class="error-col-badge">Column ${err.col}</span>` : ''}
          </div>
          <div class="jump-action">
            <span>Jump to line ${err.line}</span>
            <svg class="icon-sm" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <polyline points="9 18 15 12 9 6"/>
            </svg>
          </div>
        </div>
        ${err.message && err.message !== err.title ? `<div class="error-message">${escapeHtml(err.message)}</div>` : ''}
        ${previewLine ? `<div class="error-snippet-preview">Line ${err.line}: ${escapeHtml(previewLine)}</div>` : ''}
      </div>
    `;
  });

  DOM.errorList.innerHTML = html;
}

function renderErrorState(message) {
  DOM.statusBanner.className = 'status-banner banner-error';
  DOM.bannerIcon.textContent = '⚠️';
  DOM.bannerTitle.textContent = 'Parser Error';
  DOM.bannerSubtitle.textContent = message;
}

// ==========================================================================
// File Upload & Drag-and-Drop
// ==========================================================================

function setupFileUpload() {
  DOM.uploadBtn.addEventListener('click', () => {
    DOM.fileInput.value = '';
    DOM.fileInput.click();
  });

  DOM.fileInput.addEventListener('change', async (e) => {
    const file = e.target.files[0];
    if (file) {
      await handleFile(file);
    }
  });

  // Drag and Drop support
  const dropZone = DOM.editorDropZone;

  ['dragenter', 'dragover'].forEach(eventName => {
    dropZone.addEventListener(eventName, (e) => {
      e.preventDefault();
      e.stopPropagation();
      DOM.dropOverlay.classList.add('active');
    }, false);
  });

  ['dragleave', 'drop'].forEach(eventName => {
    dropZone.addEventListener(eventName, (e) => {
      e.preventDefault();
      e.stopPropagation();
      DOM.dropOverlay.classList.remove('active');
    }, false);
  });

  dropZone.addEventListener('drop', async (e) => {
    const files = e.dataTransfer.files;
    if (files && files.length > 0) {
      await handleFile(files[0]);
    }
  });
}

async function handleFile(file) {
  const formData = new FormData();
  formData.append('file', file);

  try {
    const res = await fetch('/api/upload', {
      method: 'POST',
      body: formData
    });
    const result = await res.json();
    if (result.error) {
      alert(`Upload error: ${result.error}`);
      return;
    }

    setEditorCode(result.content);
    DOM.currentFileName.textContent = result.filename;
    DOM.fileBadge.textContent = `${formatBytes(result.size)}`;
    DOM.sampleSelect.value = '';

    // Automatically run parser on upload
    runParser();

  } catch (err) {
    console.error('File read error:', err);
    // Client-side fallback read
    const reader = new FileReader();
    reader.onload = (e) => {
      setEditorCode(e.target.result);
      DOM.currentFileName.textContent = file.name;
      DOM.fileBadge.textContent = `${formatBytes(file.size)}`;
      DOM.sampleSelect.value = '';
      runParser();
    };
    reader.readAsText(file);
  }
}

// ==========================================================================
// Sample Selector Management
// ==========================================================================

async function loadSamplesList() {
  try {
    const res = await fetch('/api/samples');
    const samples = await res.json();
    state.samples = samples;

    // Group samples by category
    const categories = {};
    samples.forEach(s => {
      const cat = s.category || 'Samples';
      if (!categories[cat]) categories[cat] = [];
      categories[cat].push(s);
    });

    DOM.sampleSelect.innerHTML = '<option value="" disabled selected>Select a sample...</option>';

    for (const [cat, list] of Object.entries(categories)) {
      const optGroup = document.createElement('optgroup');
      optGroup.label = cat;
      list.forEach(item => {
        const opt = document.createElement('option');
        opt.value = item.id;
        opt.textContent = item.name;
        optGroup.appendChild(opt);
      });
      DOM.sampleSelect.appendChild(optGroup);
    }

    DOM.sampleSelect.addEventListener('change', async (e) => {
      const id = e.target.value;
      if (id) {
        await loadSampleById(id);
      }
    });

  } catch (err) {
    console.error('Failed to load samples:', err);
  }
}

async function loadSampleById(id) {
  try {
    const res = await fetch(`/api/sample/${encodeURIComponent(id)}`);
    const data = await res.json();
    if (data.content) {
      setEditorCode(data.content);
      DOM.currentFileName.textContent = data.filename;
      DOM.fileBadge.textContent = 'C++';
      runParser();
    }
  } catch (err) {
    console.error('Error fetching sample:', err);
  }
}

async function loadInitialSample() {
  await loadSamplesList();
  // Load default errors.cpp sample
  await loadSampleById('errors.cpp');
  DOM.sampleSelect.value = 'errors.cpp';
}

// ==========================================================================
// Tabs & UI Controls
// ==========================================================================

function switchTab(tabId) {
  DOM.tabButtons.forEach(btn => {
    btn.classList.toggle('active', btn.dataset.tab === tabId);
  });
  DOM.tabPanes.forEach(pane => {
    pane.classList.toggle('active', pane.id === tabId);
  });
}

function setupTabs() {
  DOM.tabButtons.forEach(btn => {
    btn.addEventListener('click', () => {
      switchTab(btn.dataset.tab);
    });
  });
}

function setupActions() {
  // Run button click
  DOM.runBtn.addEventListener('click', runParser);

  // Keyboard shortcut Ctrl+Enter / Cmd+Enter
  window.addEventListener('keydown', (e) => {
    if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
      e.preventDefault();
      runParser();
    }
  });

  // Clear Editor
  DOM.clearBtn.addEventListener('click', () => {
    if (confirm('Clear the code editor?')) {
      setEditorCode('');
      DOM.currentFileName.textContent = '[empty file]';
      DOM.sampleSelect.value = '';
      clearErrorMarkers();
      DOM.statusBanner.className = 'status-banner banner-idle';
      DOM.bannerIcon.textContent = '⚡';
      DOM.bannerTitle.textContent = 'Parser Ready';
      DOM.bannerSubtitle.textContent = 'Enter C++ code and click Run Parser.';
      DOM.errorList.innerHTML = '';
      DOM.rawOutput.textContent = '// CLI output will appear here...';
      DOM.astOutput.textContent = '// Syntax tree will appear here...';
      DOM.tokensOutput.textContent = '// Tokens will appear here...';
    }
  });

  // Copy Code
  DOM.copyCodeBtn.addEventListener('click', () => {
    navigator.clipboard.writeText(getEditorCode()).then(() => {
      const orig = DOM.copyCodeBtn.innerHTML;
      DOM.copyCodeBtn.innerHTML = '<span>Copied!</span>';
      setTimeout(() => DOM.copyCodeBtn.innerHTML = orig, 1500);
    });
  });

  // Download Code
  DOM.downloadCodeBtn.addEventListener('click', () => {
    const code = getEditorCode();
    const filename = DOM.currentFileName.textContent || 'source.cpp';
    const blob = new Blob([code], { type: 'text/x-c++src' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename.endsWith('.cpp') || filename.endsWith('.h') ? filename : `${filename}.cpp`;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
  });

  // Copy Output Buttons
  setupCopyButton(DOM.copyRawBtn, () => DOM.rawOutput.textContent);
  setupCopyButton(DOM.copyAstBtn, () => DOM.astOutput.textContent);
  setupCopyButton(DOM.copyTokensBtn, () => DOM.tokensOutput.textContent);

  // Theme Toggle
  DOM.themeToggleBtn.addEventListener('click', toggleTheme);
}

function setupCopyButton(btn, getTextFn) {
  if (!btn) return;
  btn.addEventListener('click', () => {
    navigator.clipboard.writeText(getTextFn()).then(() => {
      const orig = btn.textContent;
      btn.textContent = 'Copied!';
      setTimeout(() => btn.textContent = orig, 1500);
    });
  });
}

function toggleTheme() {
  state.isDarkTheme = !state.isDarkTheme;
  document.body.classList.toggle('theme-dark', state.isDarkTheme);
  document.body.classList.toggle('theme-light', !state.isDarkTheme);

  DOM.moonIcon.style.display = state.isDarkTheme ? 'block' : 'none';
  DOM.sunIcon.style.display = state.isDarkTheme ? 'none' : 'block';

  if (state.monacoEditor) {
    monaco.editor.setTheme(state.isDarkTheme ? 'vs-dark' : 'vs');
  }
}

// Server Health Check
async function checkServerHealth() {
  try {
    const res = await fetch('/api/status');
    const data = await res.json();
    if (data.status === 'online') {
      DOM.serverStatusDot.style.backgroundColor = 'var(--color-success)';
      DOM.serverStatusText.textContent = 'Parser online';
    }
  } catch (err) {
    DOM.serverStatusDot.style.backgroundColor = 'var(--color-error)';
    DOM.serverStatusText.textContent = 'Server disconnected';
  }
}

// ==========================================================================
// Utilities
// ==========================================================================

function escapeHtml(str) {
  if (!str) return '';
  return str
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#039;');
}

function formatBytes(bytes, decimals = 1) {
  if (bytes === 0) return '0 B';
  const k = 1024;
  const dm = decimals < 0 ? 0 : decimals;
  const sizes = ['B', 'KB', 'MB', 'GB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return parseFloat((bytes / Math.pow(k, i)).toFixed(dm)) + ' ' + sizes[i];
}

// Expose jumpToLine to window for onclick handlers
window.jumpToLine = jumpToLine;

// Application Entry Point
document.addEventListener('DOMContentLoaded', () => {
  setupFileUpload();
  setupTabs();
  setupActions();
  initMonacoEditor();
  checkServerHealth();
});

