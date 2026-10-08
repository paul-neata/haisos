#include "RegexTree.h"

#include <cstdio>
#include <string>

namespace Haisos {

namespace {

// One byte as \xhh, two lowercase hex digits.
std::string HexByte(unsigned char c) {
    char buffer[5];
    std::snprintf(buffer, sizeof(buffer), "\\x%02x", static_cast<int>(c));
    return std::string(buffer);
}

// A byte inside a [...] set: itself when printable and not one of the set's
// own characters, \xhh otherwise.
std::string SetByte(unsigned char c) {
    if (c >= 0x21 && c <= 0x7e && c != '[' && c != ']' && c != '-' && c != '\\' && c != '\'')
        return std::string(1, static_cast<char>(c));
    return HexByte(c);
}

std::string AssertName(RegexAssertion assertion, bool multiline) {
    switch (assertion) {
    case RegexAssertion::LineStart: return multiline ? "(bol-m)" : "(bol)";
    case RegexAssertion::LineEnd: return multiline ? "(eol-m)" : "(eol)";
    case RegexAssertion::LineEndPerl: return "(eol-perl)";
    case RegexAssertion::TextStart: return "(begbuf)";
    case RegexAssertion::TextEnd: return "(endbuf)";
    case RegexAssertion::TextEndBeforeNewline: return "(endbuf-nl)";
    case RegexAssertion::WordBoundary: return "(wordb)";
    case RegexAssertion::NotWordBoundary: return "(notwordb)";
    case RegexAssertion::WordStart: return "(wordstart)";
    case RegexAssertion::WordEnd: return "(wordend)";
    }
    return "(bol)";
}

void DumpNode(const RegexTree& tree, int index, std::string& out) {
    const RegexNode& node = tree.nodes[static_cast<size_t>(index)];
    switch (node.type) {
    case RegexNodeType::Empty:
        out += "(empty)";
        break;
    case RegexNodeType::Bytes: {
        std::string runs;
        int first = -1;
        int count = 0;
        // The set as maximal runs of consecutive byte values; the runs are
        // appended as they end, so the output is in increasing order.
        auto endRun = [&](int last) {
            if (first < 0) return;
            if (last == first) {
                runs += SetByte(static_cast<unsigned char>(first));
            } else if (last == first + 1) {
                runs += SetByte(static_cast<unsigned char>(first));
                runs += SetByte(static_cast<unsigned char>(last));
            } else {
                runs += SetByte(static_cast<unsigned char>(first));
                runs += '-';
                runs += SetByte(static_cast<unsigned char>(last));
            }
            first = -1;
        };
        for (int c = 0; c < 256; ++c) {
            if (node.bytes[static_cast<size_t>(c)]) {
                if (first < 0) first = c;
                ++count;
            } else {
                endRun(c - 1);
            }
        }
        endRun(255);
        if (count == 1) {
            // One byte: 'b' when printable, \xhh otherwise.
            for (int c = 0; c < 256; ++c) {
                if (!node.bytes[static_cast<size_t>(c)]) continue;
                unsigned char b = static_cast<unsigned char>(c);
                if (c >= 0x21 && c <= 0x7e && c != '\'' && c != '\\') {
                    out += '\'';
                    out += static_cast<char>(b);
                    out += '\'';
                } else {
                    out += HexByte(b);
                }
            }
            break;
        }
        out += '[';
        out += runs;
        out += ']';
        break;
    }
    case RegexNodeType::Concat:
    case RegexNodeType::Alternate:
        out += node.type == RegexNodeType::Concat ? "(cat " : "(alt ";
        for (size_t i = 0; i < node.children.size(); ++i) {
            if (i > 0) out += ' ';
            DumpNode(tree, node.children[i], out);
        }
        out += ')';
        break;
    case RegexNodeType::Repeat:
        out += node.greedy ? "(rep " : "(lazy ";
        out += std::to_string(node.min);
        out += ' ';
        out += node.max < 0 ? "inf" : std::to_string(node.max);
        out += ' ';
        DumpNode(tree, node.children[0], out);
        out += ')';
        break;
    case RegexNodeType::Group:
        out += "(group ";
        out += std::to_string(node.group);
        out += ' ';
        DumpNode(tree, node.children[0], out);
        out += ')';
        break;
    case RegexNodeType::Assert:
        out += AssertName(node.assertion, node.multiline);
        break;
    case RegexNodeType::BackRef:
        out += node.ignoreCase ? "(backref-i " : "(backref ";
        out += std::to_string(node.group);
        out += ')';
        break;
    }
}

} // namespace

std::string DumpRegexTree(const RegexTree& tree) {
    if (tree.root < 0) return "(empty)";
    std::string out;
    DumpNode(tree, tree.root, out);
    return out;
}

} // namespace Haisos