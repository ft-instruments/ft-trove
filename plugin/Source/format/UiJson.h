// The panel as the page receives it. Shared by the editor and by trove-render
// --ui, so a screenshot of the page can be taken against the real parser.
#pragma once

#include <juce_core/juce_core.h>
#include "Library.h"

namespace trove {

inline juce::var uiJson(const UiSpec& ui) {
  if (!ui.valid) return {};
  auto strings = [](const juce::StringArray& list) {
    juce::Array<juce::var> out;
    for (auto& s : list) out.add(s);
    return out;
  };

  // Every binding, with only the fields it actually uses: Fragile Violins
  // alone has thousands of them.
  auto bindingJson = [](const UiBinding& bd) {
    juce::DynamicObject::Ptr b = new juce::DynamicObject();
    b->setProperty("type", bd.type);
    b->setProperty("parameter", bd.parameter);
    b->setProperty("level", bd.level);
    b->setProperty("position", bd.position);
    if (bd.identifier.isNotEmpty()) b->setProperty("identifier", bd.identifier);
    if (bd.tags.isNotEmpty()) b->setProperty("tags", bd.tags);
    if (bd.groupIndex >= 0) b->setProperty("groupIndex", bd.groupIndex);
    if (bd.effectIndex >= 0) b->setProperty("effectIndex", bd.effectIndex);
    if (bd.modulatorIndex >= 0) b->setProperty("modulatorIndex", bd.modulatorIndex);
    if (bd.bindingIndex >= 0) b->setProperty("bindingIndex", bd.bindingIndex);
    if (bd.translation.isNotEmpty()) b->setProperty("translation", bd.translation);
    if (bd.table.isNotEmpty()) b->setProperty("table", bd.table);
    if (bd.fixedValue.isNotEmpty()) b->setProperty("fixed", bd.fixedValue);
    if (bd.hasFactor) b->setProperty("factor", bd.factor);
    if (!bd.triggerOnLoad) b->setProperty("triggerOnLoad", false);
    if (bd.hasOutputRange) {
      b->setProperty("outMin", bd.outMin);
      b->setProperty("outMax", bd.outMax);
    }
    return juce::var(b.get());
  };

  juce::DynamicObject::Ptr o = new juce::DynamicObject();
  o->setProperty("width", ui.width);
  o->setProperty("height", ui.height);
  o->setProperty("bgColor", ui.bgColor);
  o->setProperty("bgImage", ui.bgImage);
  o->setProperty("bgTopLeft", ui.bgTopLeft);
  o->setProperty("hasKeyboard", ui.hasKeyboard);
  juce::Array<juce::var> ranges;
  for (auto& r : ui.keyRanges) {
    juce::DynamicObject::Ptr ro = new juce::DynamicObject();
    ro->setProperty("lo", r.lo);
    ro->setProperty("hi", r.hi);
    ro->setProperty("color", r.colour);
    ranges.add(juce::var(ro.get()));
  }
  o->setProperty("keyRanges", ranges);
  o->setProperty("tabs", strings(ui.tabs));

  juce::Array<juce::var> controls;
  for (auto& c : ui.controls) {
    juce::DynamicObject::Ptr co = new juce::DynamicObject();
    co->setProperty("type", c.type);
    co->setProperty("tab", c.tab);
    co->setProperty("tags", c.tags);
    co->setProperty("labeled", c.labeled);
    co->setProperty("uiIndex", c.uiIndex);
    co->setProperty("x", c.x);
    co->setProperty("y", c.y);
    co->setProperty("w", c.w);
    co->setProperty("h", c.h);
    co->setProperty("text", c.text);
    co->setProperty("style", c.style);
    co->setProperty("source", c.source);
    co->setProperty("fg", c.fg);
    co->setProperty("bg", c.bg);
    co->setProperty("textColor", c.textColor);
    co->setProperty("textSize", c.textSize);
    co->setProperty("min", c.minValue);
    co->setProperty("max", c.maxValue);
    co->setProperty("value", c.value);
    co->setProperty("defaultValue", c.defaultValue);
    co->setProperty("valueType", c.valueType);
    co->setProperty("showLabel", c.showLabel);
    co->setProperty("visible", c.visible);
    co->setProperty("vertical", c.vertical);
    co->setProperty("dragVertical", c.dragVertical);
    co->setProperty("stretch", c.stretch);
    co->setProperty("frames", c.frames);
    co->setProperty("dragPixels", c.dragPixels);
    co->setProperty("frameRate", c.frameRate);
    co->setProperty("opacity", c.opacity);
    co->setProperty("align", c.align);
    co->setProperty("placeholder", c.placeholder);
    co->setProperty("options", strings(c.options));

    // What each menu entry does when it is picked.
    juce::Array<juce::var> optionBindings;
    for (auto& entry : c.optionBindings) {
      juce::Array<juce::var> list;
      for (auto& bd : entry) list.add(bindingJson(bd));
      optionBindings.add(juce::var(list));
    }
    co->setProperty("optionBindings", optionBindings);

    juce::Array<juce::var> states;
    for (auto& st : c.states) {
      juce::DynamicObject::Ptr so = new juce::DynamicObject();
      so->setProperty("name", st.name);
      so->setProperty("main", st.main);
      so->setProperty("click", st.click);
      so->setProperty("hover", st.hover);
      juce::Array<juce::var> stateBindings;
      for (auto& bd : st.bindings) stateBindings.add(bindingJson(bd));
      so->setProperty("bindings", stateBindings);
      states.add(juce::var(so.get()));
    }
    co->setProperty("states", states);

    juce::Array<juce::var> bindings;
    for (auto& bd : c.bindings) bindings.add(bindingJson(bd));
    co->setProperty("bindings", bindings);
    controls.add(juce::var(co.get()));
  }
  o->setProperty("controls", controls);
  return juce::var(o.get());
}

} // namespace trove
