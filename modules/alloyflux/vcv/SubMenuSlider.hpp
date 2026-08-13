#pragma once
#include <rack.hpp>

// ---------------------------------------------------------------------------
// SubMenuSlider — horizontal drag slider + submenu text field for a Quantity.
//
// Adapted from stoermelder's SubMenuSlider (PACK-ONE, MIT licence).
// Drop-in for MenuItem inside appendContextMenu().
//
// Usage:
//   menu->addChild(createMenuLabel("Fatness"));
//   auto* s = new SubMenuSlider;
//   s->quantity = module->getParamQuantity(AlloyFlux::FATNESS_PARAM);
//   menu->addChild(s);
// ---------------------------------------------------------------------------
struct SubMenuSlider : rack::ui::MenuItem
{
    static constexpr float SENSITIVITY  = 0.001f;
    static constexpr float SLIDER_WIDTH = 200.f; // wider than default menu item

    /** Not owned. */
    rack::Quantity *quantity = nullptr;

    SubMenuSlider()
    {
        box.size.y = BND_WIDGET_HEIGHT;
        box.size.x = SLIDER_WIDTH;
    }

    void step() override
    {
        rack::ui::MenuItem::step();
        box.size.x = SLIDER_WIDTH; // prevent parent menu from shrinking us
    }

    void draw(const rack::widget::Widget::DrawArgs &args) override
    {
        BNDwidgetState state = BND_DEFAULT;
        if(APP->event->hoveredWidget == this)
            state = BND_HOVER;
        if(APP->event->draggedWidget == this)
            state = BND_ACTIVE;

        float       progress = quantity ? quantity->getScaledValue() : 0.f;
        std::string text     = quantity ? quantity->getString() : "";

        rack::ui::Menu *parentMenu
            = dynamic_cast<rack::ui::Menu *>(getParent());
        int flags = parentMenu ? BND_CORNER_ALL : BND_CORNER_NONE;
        bndSlider(args.vg,
                  0.f,
                  0.f,
                  box.size.x,
                  box.size.y,
                  flags,
                  state,
                  progress,
                  text.c_str(),
                  nullptr);
    }

    void onDragDrop(const rack::event::DragDrop &) override {}

    void onDragStart(const rack::event::DragStart &e) override
    {
        if(e.button != GLFW_MOUSE_BUTTON_LEFT)
            return;
        APP->window->cursorLock();
    }

    void onDragMove(const rack::event::DragMove &e) override
    {
        if(quantity)
            quantity->moveScaledValue(SENSITIVITY * e.mouseDelta.x);
    }

    void onDragEnd(const rack::event::DragEnd &) override
    { APP->window->cursorUnlock(); }

    void onDoubleClick(const rack::event::DoubleClick &) override
    {
        if(quantity)
            quantity->reset();
    }

    rack::ui::Menu *createChildMenu() override
    {
        struct SliderField : rack::ui::TextField
        {
            rack::Quantity *quantity;
            bool            textSync = true;
            SliderField() { box.size.x = 150.f; }

            void onSelectKey(const rack::event::SelectKey &e) override
            {
                if(e.action == GLFW_PRESS && e.key == GLFW_KEY_ENTER)
                {
                    float v;
                    if(std::sscanf(text.c_str(), "%f", &v) == 1)
                        quantity->setDisplayValue(v);
                    e.consume(this);
                }
                if(!e.getTarget())
                    rack::ui::TextField::onSelectKey(e);
            }

            void step() override
            {
                if(textSync)
                    text = quantity->getDisplayValueString();
                TextField::step();
            }

            void onButton(const rack::event::Button &e) override
            {
                textSync = false;
                TextField::onButton(e);
            }
        };

        auto *menu      = new rack::ui::Menu;
        auto *field     = new SliderField;
        field->quantity = quantity;
        menu->addChild(field);
        return menu;
    }
};
