;;; fulcrum-mode.el --- major mode for Fulcrum

;; Author: Alexey Egorov
;; Version: 0.0.1
;; Created: 2022-10-25

;;; Commentary:

;; The major mode for editing Fulcrum code.

;;; Code:

(require 'lisp-mode)

(define-abbrev-table 'fulcrum-mode-abbrev-table ()
  "Abbrev table for Fulcrum mode.
It has `lisp-mode-abbrev-table' as its parent."
  :parents (list lisp-mode-abbrev-table))

(defvar fulcrum-mode-syntax-table
  (let ((table (make-syntax-table lisp-data-mode-syntax-table)))
    table))

(defvar fulcrum-mode-map
  (let ((map (make-sparse-keymap)))
    (set-keymap-parent map lisp-mode-shared-map)
    map)
  "Keymap for Fulcrum mode.
All commands in `lisp-mode-shared-map' are inherited by this map.")

(defcustom fulcrum-mode-hook nil
  "Hook run when entering Fulcrum mode."
  :type 'hook
  :group 'lisp)

(defvar fulcrum-mode-line-process "")

(defconst fulcrum-font-lock-keywords
  (eval-when-compile
    (list
     (list (concat "(\\("
                   ;; module
                   "module\\|"
                   ;; struct, public or private
                   "struct-?\\|"
                   ;; fn, public or private
                   "fn-?\\|"
                   ;; var
                   "var"
                   "\\)"
                   "[ \t\n]*"
                   "\\(\\sw+\\)?")
           '(1 font-lock-keyword-face)
           '(2 font-lock-function-name-face))
     ;; Declarations
     (cons (concat "("
                   (regexp-opt
                    '("do"
                      "while"
                      "unless"
                      "return"
                      "if")
                    t)
                   "//>")
           font-lock-keyword-face)
     ;; Types
     ;; Keywrods
     )))

(defun fulcrum-mode-set-variables ()
  (set-syntax-table fulcrum-mode-syntax-table)
  (setq local-abbrev-table fulcrum-mode-abbrev-table)
  (setq mode-line-process '("" fulcrum-mode-line-process))
  (setq font-lock-defaults
        '((fulcrum-font-lock-keywords)
          nil nil
          (("+-*/.<>=!?$%_&:" . "w"))
          nil
          (font-lock-mark-block-function . mark-defun)))
  (setq-local prettify-symbols-alist lisp-prettify-symbols-alist))

(put 'fn 'lisp-indent-function 'defun)
(put 'fn- 'lisp-indent-function 'defun)
(put 'module 'lisp-indent-function 'defun)
(put 'var 'lisp-indent-function 0)

;;;###autoload
(define-derived-mode fulcrum-mode lisp-data-mode "Fulcrum"
  "Major mode for editing Fulcrum code.
Editing commands are similar to those of `lisp-mode'.

Commands:
Delete converts tabs to spaces as it moves back.
Blank lines separate paragraphs.  Semicolons start comments.
\\{fulcrum-mode-map}"
  :group 'lisp
  (fulcrum-mode-set-variables))

(provide 'fulcrum-mode)
