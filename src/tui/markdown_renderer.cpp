#include "markdown_renderer.h"
#include "styles.h"
#include <md4c.h>
#include <vector>
#include <stack>
#include <string>

namespace a0::tui {

struct MDBlock {
    MD_BLOCKTYPE type;
    int headingLevel = 0;
    ftxui::Element elem;
    std::vector<ftxui::Element> children;
};

struct MarkdownRendererImpl {
    std::stack<MDBlock> blockStack;
    ftxui::Element root;
    bool streaming = false;

    void pushBlock(MD_BLOCKTYPE type) {
        MDBlock block;
        block.type = type;
        blockStack.push(std::move(block));
    }

    void popBlock() {
        if (blockStack.empty()) return;
        auto block = std::move(blockStack.top());
        blockStack.pop();

        ftxui::Element result;
        switch (block.type) {
            case MD_BLOCK_DOC:
                result = ftxui::vbox(std::move(block.children));
                break;
            case MD_BLOCK_P:
                result = ftxui::hbox(std::move(block.children));
                break;
            case MD_BLOCK_H: {
                auto h = ftxui::hbox(std::move(block.children));
                if (block.headingLevel <= 2) {
                    result = h | style::heading1;
                } else {
                    result = h | style::heading2;
                }
                break;
            }
            case MD_BLOCK_UL:
            case MD_BLOCK_OL:
                result = ftxui::vbox(std::move(block.children));
                break;
            case MD_BLOCK_CODE:
                result = ftxui::vbox(std::move(block.children)) | style::codeBlock;
                break;
            case MD_BLOCK_HR:
                result = ftxui::separator();
                break;
            default:
                result = ftxui::vbox(std::move(block.children));
                break;
        }

        if (!blockStack.empty()) {
            blockStack.top().children.push_back(std::move(result));
        } else {
            root = std::move(result);
        }
    }

    void addText(MD_TEXTTYPE type, const std::string& text) {
        ftxui::Element elem;
        switch (type) {
            case MD_TEXT_CODE:
                elem = ftxui::text(text) | style::inlineCode;
                break;
            case MD_TEXT_HTML:
                elem = ftxui::text(text) | ftxui::dim;
                break;
            default:
                elem = ftxui::text(text);
                break;
        }
        if (!blockStack.empty()) {
            blockStack.top().children.push_back(std::move(elem));
        }
    }

    void applySpanStyle(MD_SPANTYPE type, ftxui::Element& elem) {
        switch (type) {
            case MD_SPAN_EM:
                elem = elem | ftxui::italic;
                break;
            case MD_SPAN_STRONG:
                elem = elem | ftxui::bold;
                break;
            case MD_SPAN_A:
                elem = elem | style::link;
                break;
            case MD_SPAN_DEL:
                elem = elem | style::dimmed;
                break;
            case MD_SPAN_CODE:
                elem = elem | style::inlineCode;
                break;
            case MD_SPAN_U:
                elem = elem | ftxui::underlined;
                break;
            default:
                break;
        }
    }
};

static int xEnterBlock(MD_BLOCKTYPE type, void* detail, void* userdata) {
    auto* impl = static_cast<MarkdownRendererImpl*>(userdata);
    impl->pushBlock(type);
    if (type == MD_BLOCK_H && detail) {
        auto* hd = static_cast<MD_BLOCK_H_DETAIL*>(detail);
        if (!impl->blockStack.empty()) {
            impl->blockStack.top().headingLevel = hd->level;
        }
    }
    return 0;
}

static int xLeaveBlock(MD_BLOCKTYPE type, void* /*detail*/, void* userdata) {
    auto* impl = static_cast<MarkdownRendererImpl*>(userdata);
    if (!impl->blockStack.empty() && impl->blockStack.top().type == type) {
        impl->popBlock();
    }
    return 0;
}

static int xEnterSpan(MD_SPANTYPE /*type*/, void* /*detail*/, void* /*userdata*/) {
    return 0;
}

static int xLeaveSpan(MD_SPANTYPE /*type*/, void* /*detail*/, void* /*userdata*/) {
    return 0;
}

static int xText(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata) {
    auto* impl = static_cast<MarkdownRendererImpl*>(userdata);
    impl->addText(type, std::string(text, static_cast<std::string::size_type>(size)));
    return 0;
}

class MarkdownRenderer::Impl {
public:
    MarkdownRendererImpl mdImpl;
};

MarkdownRenderer::MarkdownRenderer()
    : m_impl(std::make_unique<Impl>()) {}

MarkdownRenderer::~MarkdownRenderer() = default;

ftxui::Element MarkdownRenderer::render(const std::string& md, bool streaming) {
    m_impl->mdImpl = MarkdownRendererImpl();
    m_impl->mdImpl.streaming = streaming;
    m_impl->mdImpl.root = ftxui::emptyElement();

    MD_PARSER parser = {};
    parser.flags = MD_FLAG_COLLAPSEWHITESPACE;
    if (streaming) {
        parser.flags |= MD_FLAG_PERMISSIVEURLAUTOLINKS;
    }
    parser.enter_block = xEnterBlock;
    parser.leave_block = xLeaveBlock;
    parser.enter_span = xEnterSpan;
    parser.leave_span = xLeaveSpan;
    parser.text = xText;

    md_parse(md.c_str(), md.size(), &parser, &m_impl->mdImpl);

    while (!m_impl->mdImpl.blockStack.empty()) {
        m_impl->mdImpl.popBlock();
    }

    return m_impl->mdImpl.root;
}

ftxui::Element MarkdownRenderer::renderInline(const std::string& md) {
    return render(md, false);
}

} // namespace a0::tui
