#include "export_pdf.hpp"
#include "canvas_pdf.hpp"
#include "util/podofo_inc.hpp"
#include "util/util.hpp"
#include "schematic/schematic.hpp"
#include "export_pdf_util.hpp"
#include "schematic/iinstancce_mapping_provider.hpp"
#include "util/bbox_accumulator.hpp"
#include "pool/part.hpp"

#include <iostream>

namespace horizon {

static void cb_nop(std::string, double)
{
}

class MyInstanceMappingProvider : public IInstanceMappingProvider {
public:
    MyInstanceMappingProvider(const Schematic &sch) : top(sch)
    {
    }
    void set_instance_path(const UUIDVec &p)
    {
        instance_path = p;
    }

    const class BlockInstanceMapping *get_block_instance_mapping() const override
    {
        if (instance_path.size())
            return &top.block->block_instance_mappings.at(instance_path);
        else
            return nullptr;
    }

    unsigned int get_sheet_index_for_path(const class UUID &sheet, const UUIDVec &path) const
    {
        return top.sheet_mapping.sheet_numbers.at(uuid_vec_append(path, sheet));
    }

    unsigned int get_sheet_index(const class UUID &sheet) const override
    {
        return get_sheet_index_for_path(sheet, instance_path);
    }

    unsigned int get_sheet_total() const override
    {
        return top.sheet_mapping.sheet_total;
    }


private:
    const Schematic &top;
    UUIDVec instance_path;
};

#if PODOFO_VERSION_MAJOR != 0 || PODOFO_VERSION_MINOR != 9 || PODOFO_VERSION_PATCH != 5
#define HAVE_OUTLINE
#endif

using Callback = std::function<void(std::string, double)>;

class PDFExporter {
public:
    PDFExporter(const class PDFExportSettings &settings, Callback callback)
        : document(), font(*document.GetFonts().SearchFont("Helvetica")),
        canvas(painter, font, settings),
        cb(callback),
        filename(settings.output_filename.c_str())
    {
        canvas.use_layer_colors = false;
    }

    void export_pdf(const class Schematic &sch)
    {
        std::cout << "save AA" << std::endl;
        cb("Initializing", 0);
        document.GetMetadata().SetCreator(PoDoFo::PdfString("Horizon EDA"));
        document.GetMetadata().SetProducer(PoDoFo::PdfString("Horizon EDA"));
        if (sch.block->project_meta.count("author")) {
            document.GetMetadata().SetAuthor(PoDoFo::PdfString(sch.block->project_meta.at("author")));
        }
        std::string title = "Schematic";
        if (sch.block->project_meta.count("project_title")) {
            title = sch.block->project_meta.at("project_title");
        }
        document.GetMetadata().SetTitle(PoDoFo::PdfString(title));
        MyInstanceMappingProvider prv(sch);
#if 0
#ifdef HAVE_OUTLINE
        outlines = document.GetOutlines();
        PoDoFo::PdfOutlineItem *proot = nullptr;
#else
        PoDoFo::PdfOutlineItem *proot = nullptr;
#endif
#endif
        std::cout << "save KK" << std::endl;

        //export_schematic(sch, {}, prv, proot);
        export_schematic(sch, {}, prv);
#if 0
        std::cout << "save LL" << std::endl;
        for (auto &[path, number, rect] : annotations) {
            auto &page = document.GetPages().GetPageAt(number);
            auto &annot = page.GetAnnotations().CreateAnnot<PoDoFo::PdfAnnotationLink>(rect);
            annot.SetBorderStyle(0, 0, 0);
            //annot.SetDestination(std::make_shared<PoDoFo::PdfDestination>(first_pages.at(path)));
        }
        for (auto &[url, number, rect] : datasheet_annotations) {
            auto &page = document.GetPages().GetPageAt(number);
            auto &annot = page.GetAnnotations().CreateAnnot<PoDoFo::PdfAnnotationLink>(rect);
            annot.SetBorderStyle(0, 0, 0);

            PoDoFo::PdfAction action(document, PoDoFo::PdfActionType::URI);
            action.SetURI(PoDoFo::PdfString(url));
            //TODO no idea: annot.SetAction(action);
        }
#endif
        document.Save(filename);
        std::cout << "save ZZ" << std::endl;
    }

private:
    PoDoFo::PdfMemDocument document;
    PoDoFo::PdfPainter painter;
    PoDoFo::PdfFont &font;
    //std::map<UUIDVec, PoDoFo::PdfDestination> first_pages;
    //std::vector<std::tuple<UUIDVec, unsigned int, PoDoFo::Rect>> annotations;
    //std::vector<std::tuple<std::string, unsigned int, PoDoFo::Rect>> datasheet_annotations;
    //PoDoFo::PdfOutlines *outlines = nullptr;
    CanvasPDF canvas;
    Callback cb;
    std::basic_string_view<char> filename;

    void export_schematic(const Schematic &sch, const UUIDVec &path, MyInstanceMappingProvider &prv)
    {
        std::cout << "save AAA" << std::endl;
        if (Block::instance_path_too_long(path, __FUNCTION__))
            return;
        prv.set_instance_path(path);
        Schematic my_sch = sch;
        my_sch.expand(false, &prv);
        //bool first = true;
        auto sheets = my_sch.get_sheets_sorted();
        for (const auto sheet : sheets) {

            auto rmode = painter.TextState.GetRenderingMode();

            std::cout << "save BBB rmode:" << (int)rmode << std::endl;
            const auto idx = prv.get_sheet_index_for_path(sheet->uuid, path);
            const auto progress = (double)idx / prv.get_sheet_total();
            cb("Exporting sheet " + format_m_of_n(idx, prv.get_sheet_total()), progress);
            auto &page =
                    document.GetPages().CreatePage(PoDoFo::Rect(0, 0, to_pt(sheet->frame.width), to_pt(sheet->frame.height)));
            painter.SetCanvas(page);

#if 1
            painter.GraphicsState.SetLineCapStyle(PoDoFo::PdfLineCapStyle::Round);
            painter.GraphicsState.SetFillColor(PoDoFo::PdfColor(0, 0, 0));
            painter.TextState.SetFont(font, 10);
            painter.TextState.SetRenderingMode(PoDoFo::PdfTextRenderingMode::Invisible);
#endif

            for (const auto &[uu, pic] : sheet->pictures) {
                if (!pic.on_top)
                    render_picture(document, painter, pic);
            }

            for (const auto &[uu_sym, sym] : sheet->block_symbols) {
                for (const auto &[uu, pic] : sym.symbol.pictures) {
                    if (!pic.on_top)
                        render_picture(document, painter, pic, sym.placement);
                }
            }

            auto rmode2 = painter.TextState.GetRenderingMode();
            std::cout << "save CCC rmode:" << (int)rmode2 << std::endl;

            //painter.GraphicsState.SetLineCapStyle(PoDoFo::PdfLineCapStyle::Round);
            //painter.GraphicsState.SetFillColor(PoDoFo::PdfColor(0, 0, 0));
            //painter.TextState.SetFont(font, 10);
            //painter.TextState.SetRenderingMode(PoDoFo::PdfTextRenderingMode::Invisible);
            canvas.update(*sheet);

            for (const auto &[uu, pic] : sheet->pictures) {
                if (pic.on_top)
                    render_picture(document, painter, pic);
            }
            for (const auto &[uu_sym, sym] : sheet->block_symbols) {
                for (const auto &[uu, pic] : sym.symbol.pictures) {
                    if (pic.on_top)
                        render_picture(document, painter, pic, sym.placement);
                }
            }
#if 0
std::cout << "save FFF" << std::endl;
            auto dest = PoDoFo::PdfDestination(page);
            if (first) {
                //first_pages.emplace(path, dest);
                first = false;
            }
if(1)
            {
                const auto &items = canvas.get_selectables().get_items();
                const auto &items_ref = canvas.get_selectables().get_items_ref();
                const auto n = items.size();

                std::cout << "save LLL" << std::endl;
                for (size_t i = 0; i < n; i++) {
                    const auto &it = items.at(i);
                    const auto &ir = items_ref.at(i);
                    if (ir.type == ObjectType::SCHEMATIC_BLOCK_SYMBOL) {
                        if (it.is_box()) {
                            std::cout << "save NNN" << std::endl;
                            BBoxAccumulator<float> acc;
                            for (const auto &c : it.get_corners()) {
                                acc.accumulate(c);
                            }
                            const auto [a, b] = acc.get();
                            PoDoFo::Rect rect(to_pt(a.x), to_pt(a.y), to_pt(b.x - a.x), to_pt(b.y - a.y));
                            annotations.emplace_back(
                                    uuid_vec_append(path, sheet->block_symbols.at(ir.uuid).block_instance->uuid),
                                    page.GetPageNumber() - 1, rect);
                        }
                    }
                    else if (ir.type == ObjectType::SCHEMATIC_SYMBOL) {
                        if (it.is_box()) {
                            const auto &sym = sheet->symbols.at(ir.uuid);
                            if (sym.component->part && sym.component->part->get_datasheet().size()) {
                                std::cout << "save RRR" << std::endl;
                                BBoxAccumulator<float> acc;

                                for (const auto &c : it.get_corners()) {
                                    acc.accumulate(c);
                                }
                                const auto [a, b] = acc.get();
                                PoDoFo::Rect rect(to_pt(a.x), to_pt(a.y), to_pt(b.x - a.x), to_pt(b.y - a.y));
                                datasheet_annotations.emplace_back(sym.component->part->get_datasheet(),
                                                                   page.GetPageNumber() - 1, rect);
                            }
                        }
                    }
                }
            }
#endif
std::cout << "save UUU" << std::endl;
            painter.FinishDrawing();

#if 0
#ifdef HAVE_OUTLINE
            //PoDoFo::PdfOutlineItem *sheet_node;
            if (parent) {
                //sheet_node = parent->CreateChild(PoDoFo::PdfString(sheet->name), std::make_shared<PoDoFo::PdfDestination>(dest));
            }
            else {
                //sheet_node = outlines->CreateRoot(PoDoFo::PdfString(sheet->name));
                //sheet_node->SetDestination(std::make_shared<PoDoFo::PdfDestination>(dest));
            }
#endif
            for (auto sym : sheet->get_block_symbols_sorted()) {
#ifdef HAVE_OUTLINE
                auto sym_node = sheet_node->CreateChild(PoDoFo::PdfString(sym->block_instance->refdes),  std::make_shared<PoDoFo::PdfDestination>(dest));
                sym_node->SetTextFormat(PoDoFo::PdfOutlineFormat::Italic);
#else
                PoDoFo::PdfOutlineItem *sym_node = nullptr;
#endif
                export_schematic(*sym->schematic, uuid_vec_append(path, sym->block_instance->uuid), prv, sym_node);
            }
#endif
        }
        std::cout << "save ZZZ" << std::endl;
    }
};

void export_pdf(const class Schematic &sch, const class PDFExportSettings &settings, Callback cb)
{
    if (!cb)
        cb = &cb_nop;
    std::cout << "save A" << std::endl;
    PDFExporter ex(settings, cb);
    std::cout << "save B" << std::endl;
    ex.export_pdf(sch);
    std::cout << "save C" << std::endl;
}
} // namespace horizon
