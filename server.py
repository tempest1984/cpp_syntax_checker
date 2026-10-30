#!/usr/bin/env python3
"""
C++ Parser Web Application Server
Provides a modern web UI, code editor, file uploader, and REST API
backed by cpp_parser.exe (C++17 recursive-descent parser).
"""

import os
import sys
import time
import subprocess
import re
from pathlib import Path
from flask import Flask, request, jsonify, send_from_directory, render_template

BASE_DIR = Path(__file__).resolve().parent
TEMPLATES_DIR = BASE_DIR / "templates"
STATIC_DIR = BASE_DIR / "static"
SAMPLES_DIR = BASE_DIR / "samples"
TESTS_DIR = BASE_DIR / "tests" / "cases"

# Locate cpp_parser binary
PARSER_BIN = None
for candidate in [
    BASE_DIR / "cpp_parser.exe",
    BASE_DIR / "cpp_parser",
    BASE_DIR / "build" / "cpp_parser.exe",
    BASE_DIR / "build" / "cpp_parser",
    BASE_DIR / "build" / "Release" / "cpp_parser.exe",
    BASE_DIR / "build" / "Debug" / "cpp_parser.exe",
]:
    if candidate.is_file():
        PARSER_BIN = candidate
        break

if not PARSER_BIN:
    # fallback to command in PATH
    PARSER_BIN = "cpp_parser"

app = Flask(__name__, template_folder=str(TEMPLATES_DIR), static_folder=str(STATIC_DIR))
app.config['MAX_CONTENT_LENGTH'] = 16 * 1024 * 1024  # 16MB max upload

ERROR_LINE_RE = re.compile(r'^Error at line (\d+)(?:\s+\(col (\d+)\):\s+(.*))?$')
ERROR_SUMMARY_RE = re.compile(r'^(\d+)\s+errors?\s+found')

SAMPLE_CATALOG = [
    {
        "id": "errors.cpp",
        "path": "samples/errors.cpp",
        "name": "errors.cpp (Syntax Errors)",
        "category": "Errors",
        "description": "Demonstrates missing paren, missing brace, and undeclared type Student"
    },
    {
        "id": "valid.cpp",
        "path": "samples/valid.cpp",
        "name": "valid.cpp (Valid ISO C++17)",
        "category": "Valid",
        "description": "Full showcase: namespaces, templates, lambdas, structured bindings, exceptions"
    },
    {
        "id": "if_missing_paren.cpp",
        "path": "tests/cases/if_missing_paren.cpp",
        "name": "if_missing_paren.cpp",
        "category": "Errors",
        "description": "Missing closing parenthesis in if-statement condition"
    },
    {
        "id": "class_missing_semicolon.cpp",
        "path": "tests/cases/class_missing_semicolon.cpp",
        "name": "class_missing_semicolon.cpp",
        "category": "Errors",
        "description": "Class declaration missing trailing semicolon after closing brace"
    },
    {
        "id": "expr_missing_operand.cpp",
        "path": "tests/cases/expr_missing_operand.cpp",
        "name": "expr_missing_operand.cpp",
        "category": "Errors",
        "description": "Binary expression with missing right-hand operand"
    },
    {
        "id": "undeclared_type.cpp",
        "path": "tests/cases/undeclared_type.cpp",
        "name": "undeclared_type.cpp",
        "category": "Errors",
        "description": "Type name used without prior declaration"
    },
    {
        "id": "unterminated_string.cpp",
        "path": "tests/cases/unterminated_string.cpp",
        "name": "unterminated_string.cpp",
        "category": "Errors",
        "description": "String literal missing closing quote"
    },
    {
        "id": "stray_brace.cpp",
        "path": "tests/cases/stray_brace.cpp",
        "name": "stray_brace.cpp",
        "category": "Errors",
        "description": "Unexpected stray brace without matching open brace"
    },
    {
        "id": "multiple.cpp",
        "path": "tests/cases/multiple.cpp",
        "name": "multiple.cpp",
        "category": "Errors",
        "description": "Multiple syntax errors across consecutive lines"
    },
    {
        "id": "nested_templates.cpp",
        "path": "tests/cases/nested_templates.cpp",
        "name": "nested_templates.cpp",
        "category": "Valid",
        "description": "Complex template declarations and nested specialization"
    },
    {
        "id": "declared_types.cpp",
        "path": "tests/cases/declared_types.cpp",
        "name": "declared_types.cpp",
        "category": "Valid",
        "description": "Structs, typedefs, enums and alias declarations"
    },
    {
        "id": "user_header.cpp",
        "path": "tests/cases/user_header.cpp",
        "name": "user_header.cpp",
        "category": "Valid",
        "description": "User header inclusion with opaque header support"
    }
]


def run_cpp_parser(code: str, flags: list) -> tuple:
    """Run cpp_parser binary with flags and return (stdout, stderr, exit_code, duration_ms)"""
    cmd = [str(PARSER_BIN)] + flags + ["-"]
    start = time.perf_counter()
    try:
        proc = subprocess.run(
            cmd,
            input=code,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            cwd=str(BASE_DIR),
            timeout=10
        )
        duration_ms = (time.perf_counter() - start) * 1000.0
        return proc.stdout, proc.stderr, proc.returncode, duration_ms
    except subprocess.TimeoutExpired:
        return "", "Execution timed out (10s limit)", 2, 10000.0
    except Exception as ex:
        return "", str(ex), 2, 0.0


def parse_error_lines(verbose_output: str, standard_output: str) -> list:
    """Extract structured error objects from parser output"""
    errors = []
    
    # Prefer parsing verbose output for col and description if available
    lines_to_parse = verbose_output.strip().splitlines() if verbose_output else standard_output.strip().splitlines()
    
    for raw_line in lines_to_parse:
        m = ERROR_LINE_RE.match(raw_line.strip())
        if m:
            line_num = int(m.group(1))
            col_num = int(m.group(2)) if m.group(2) else None
            msg = m.group(3).strip() if m.group(3) else f"Error at line {line_num}"
            
            # Determine error severity/type
            err_type = "syntax"
            if "unknown type name" in msg:
                err_type = "undeclared_type"
            elif "expected" in msg:
                err_type = "expected_token"
            elif "unterminated" in msg:
                err_type = "lexical"

            errors.append({
                "line": line_num,
                "col": col_num,
                "title": f"Error at line {line_num}",
                "display": f"Error at line {line_num}" + (f" (col {col_num}): {msg}" if col_num else ""),
                "message": msg,
                "type": err_type
            })
    return errors


@app.route("/")
def index():
    return render_template("index.html")


@app.route("/api/status", methods=["GET"])
def api_status():
    bin_exists = isinstance(PARSER_BIN, Path) and PARSER_BIN.is_file()
    return jsonify({
        "status": "online",
        "parser_binary": str(PARSER_BIN),
        "binary_found": bin_exists,
        "sample_count": len(SAMPLE_CATALOG)
    })


@app.route("/api/samples", methods=["GET"])
def api_samples():
    return jsonify(SAMPLE_CATALOG)


@app.route("/api/sample/<path:sample_id>", methods=["GET"])
def api_get_sample(sample_id):
    # Find item in catalog
    matched = next((s for s in SAMPLE_CATALOG if s["id"] == sample_id), None)
    file_path = BASE_DIR / matched["path"] if matched else BASE_DIR / sample_id
    
    if not file_path.is_file():
        # search samples and tests/cases
        for d in [SAMPLES_DIR, TESTS_DIR]:
            p = d / sample_id
            if p.is_file():
                file_path = p
                break

    if not file_path.is_file():
        return jsonify({"error": f"Sample '{sample_id}' not found"}), 404

    try:
        content = file_path.read_text(encoding="utf-8", errors="replace")
        return jsonify({
            "id": sample_id,
            "filename": file_path.name,
            "content": content,
            "lineCount": len(content.splitlines())
        })
    except Exception as e:
        return jsonify({"error": str(e)}), 500


@app.route("/api/upload", methods=["POST"])
def api_upload():
    if "file" not in request.files:
        return jsonify({"error": "No file uploaded"}), 400
    file = request.files["file"]
    if file.filename == "":
        return jsonify({"error": "Empty filename"}), 400

    try:
        raw_bytes = file.read()
        # Decode with utf-8 or latin-1
        try:
            content = raw_bytes.decode("utf-8")
        except UnicodeDecodeError:
            content = raw_bytes.decode("latin-1", errors="replace")

        lines = content.splitlines()
        return jsonify({
            "filename": file.filename,
            "content": content,
            "size": len(raw_bytes),
            "lineCount": len(lines)
        })
    except Exception as e:
        return jsonify({"error": f"Failed to read uploaded file: {str(e)}"}), 500


@app.route("/api/parse", methods=["POST"])
def api_parse():
    data = request.get_json(force=True, silent=True) or {}
    code = data.get("code", "")
    if isinstance(code, dict):
        code = code.get("value", "")
    elif not isinstance(code, str):
        code = str(code)

    verbose = bool(data.get("verbose", False))
    ast_requested = bool(data.get("ast", False))
    tokens_requested = bool(data.get("tokens", False))
    no_undeclared = bool(data.get("noUndeclared", False))

    if not code.strip():
        return jsonify({
            "success": True,
            "exitCode": 0,
            "errorCount": 0,
            "summary": "Empty source - no errors",
            "errors": [],
            "rawOutput": "No errors found\n",
            "ast": "",
            "tokens": "",
            "durationMs": 0.0
        })

    # Prepare flags for standard user run
    user_flags = []
    if verbose:
        user_flags.append("-v")
    if no_undeclared:
        user_flags.append("--no-undeclared")

    # Run user requested configuration
    stdout, stderr, exit_code, duration_ms = run_cpp_parser(code, user_flags)

    # We also execute with -v internally if not already verbose, so we can always extract line & col info
    if verbose:
        v_stdout = stdout
    else:
        v_flags = ["-v"]
        if no_undeclared:
            v_flags.append("--no-undeclared")
        v_stdout, _, _, _ = run_cpp_parser(code, v_flags)

    # Parse errors
    errors = parse_error_lines(v_stdout, stdout)
    error_count = len(errors)
    is_success = (exit_code == 0)

    # Summary string
    if is_success:
        summary = "No errors found"
    else:
        summary = f"{error_count} error{'s' if error_count != 1 else ''} found"

    # Optional AST
    ast_output = ""
    if ast_requested and is_success:
        ast_flags = ["--ast"]
        if no_undeclared:
            ast_flags.append("--no-undeclared")
        ast_stdout, _, _, _ = run_cpp_parser(code, ast_flags)
        # Strip the first "No errors found" line for clean AST display
        ast_lines = ast_stdout.splitlines()
        if ast_lines and "No errors found" in ast_lines[0]:
            ast_output = "\n".join(ast_lines[1:]).strip()
        else:
            ast_output = ast_stdout.strip()

    # Optional Tokens
    tokens_output = ""
    if tokens_requested:
        tokens_flags = ["--tokens"]
        tok_stdout, _, _, _ = run_cpp_parser(code, tokens_flags)
        tokens_output = tok_stdout.strip()

    return jsonify({
        "success": is_success,
        "exitCode": exit_code,
        "errorCount": error_count,
        "summary": summary,
        "errors": errors,
        "rawOutput": stdout.strip(),
        "stderr": stderr.strip(),
        "ast": ast_output,
        "tokens": tokens_output,
        "durationMs": round(duration_ms, 2)
    })


if __name__ == "__main__":
    port = int(os.environ.get("PORT", 5000))
    print(f"[*] Starting C++ Parser Web Server on http://127.0.0.1:{port}")
    print(f"[*] Parser binary: {PARSER_BIN}")
    app.run(host="127.0.0.1", port=port, debug=True)
