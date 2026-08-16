// Package symbol 所有符号的权威双向查找表。禁止其他包 hardcode 符号字符串。
package symbol

// 显示用字符串常量
const (
	ArrowLeft  = " <- "
	ArrowRight = " -> "
	ArrowEq    = " = "
)

type Entry struct {
	Word       string   // 唯一英文名，如 "add"、"lbrace"
	Glyphs     []string // 该词对应的全部符号，如 {"+"}、{"!=","≠"}
	Precedence int      // Pratt 优先级，0=非中缀
	Arith, Cmp bool
	Unary      bool
}

// byWord 英文名 → 符号定义。Word 是唯一主键。
var byWord = map[string]Entry{
	// ── 分组符 ────────────────────────────────────────────────────
	"lparen":    {"lparen",    []string{"("}, 0, false, false, false},
	"rparen":    {"rparen",    []string{")"}, 0, false, false, false},
	"lbrace":    {"lbrace",    []string{"{"}, 0, false, false, false},
	"rbrace":    {"rbrace",    []string{"}"}, 0, false, false, false},
	"lbrack":    {"lbrack",    []string{"["}, 0, false, false, false},
	"rbrack":    {"rbrack",    []string{"]"}, 0, false, false, false},
	"comma":     {"comma",     []string{","}, 0, false, false, false},
	"semicolon": {"semicolon", []string{";"}, 0, false, false, false},
	"dot":       {"dot",       []string{"."}, 0, false, false, false},
	"colon":     {"colon",     []string{":"}, 0, false, false, false},
	// ── 箭头 / 赋值（= 兼作 copy opcode）──────────────────────────
	"assign": {"assign", []string{"<-", "->", "="}, 0, false, false, false},
	// ── 算术 ──────────────────────────────────────────────────────
	"add":  {"add",  []string{"+"}, 50, true, false, true},
	"sub":  {"sub",  []string{"-"}, 50, true, false, true},
	"pointer": {"pointer", []string{"*"}, 0, false, false, false},
		"mul":  {"mul",  []string{"×"}, 60, true, false, false},
	"div":  {"div",  []string{"÷"}, 60, true, false, false},
	"mod":  {"mod",  []string{"%"}, 60, true, false, false},
	// ── 比较 ──────────────────────────────────────────────────────
	"eq":  {"eq",  []string{"=="}, 30, false, true, false},
	"neq": {"neq", []string{"!=", "≠"}, 30, false, true, false},
	"lt":  {"lt",  []string{"<"}, 40, false, true, false},
	"gt":  {"gt",  []string{">"}, 40, false, true, false},
	"le":  {"le",  []string{"<=", "≤"}, 40, false, true, false},
	"ge":  {"ge",  []string{">=", "≥"}, 40, false, true, false},
	// ── 逻辑 ──────────────────────────────────────────────────────
	"and": {"and", []string{"&&"}, 20, false, true, false},
	"or":  {"or",  []string{"||"}, 10, false, true, false},
	"not": {"not", []string{"!"}, 0, false, true, true},
	// ── 数学符号 ──────────────────────────────────────────────────
	"math.sqrt": {"math.sqrt", []string{"√"}, 0, true, false, true},

	// ── 位运算 ────────────────────────────────────────────────────
	"bitand": {"bitand", []string{"&"}, 80, true, false, false},
	"bitor":  {"bitor",  []string{"|"}, 100, true, false, false},
	"bitxor": {"bitxor", []string{"^"}, 90, true, false, false},
	"shl":    {"shl",    []string{"<<"}, 70, true, false, false},
	"shr":    {"shr",    []string{">>"}, 70, true, false, false},
}

// byGlyph 符号 → Entry（init 自动构建）。
var byGlyph = map[string]*Entry{}

func init() {
	for i := range byWord {
		e := byWord[i]
		for _, g := range e.Glyphs {
			if prev, ok := byGlyph[g]; ok {
				panic("symbol: glyph " + g + " defined in both " + prev.Word + " and " + e.Word)
			}
			byGlyph[g] = &e
		}
	}
}

// ── 公开查询 ────────────────────────────────────────────────────

var zeroEntry Entry

func Lookup(glyph string) *Entry {
	if e := byGlyph[glyph]; e != nil { return e }
	return &zeroEntry
}
func ByWord(word string) *Entry {
	if e, ok := byWord[word]; ok { return &e }
	return &zeroEntry
}

func ScannerTwoCharOps() []string {
	var ops []string
	for _, e := range byWord {
		for _, g := range e.Glyphs {
			if len(g) == 2 && isASCII(g) { ops = append(ops, g) }
		}
	}
	return ops
}

func ScannerOneCharOps() []byte {
	var ops []byte
	for _, e := range byWord {
		for _, g := range e.Glyphs {
			if len(g) == 1 && g[0] < 128 { ops = append(ops, g[0]) }
		}
	}
	return ops
}

func UnicodeOps() []string {
	var ops []string
	for _, e := range byWord {
		for _, g := range e.Glyphs {
			if !isASCII(g) { ops = append(ops, g) }
		}
	}
	return ops
}

func isASCII(s string) bool {
	for i := 0; i < len(s); i++ { if s[i] >= 128 { return false } }
	return true
}
