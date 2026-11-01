#include "ImageOutliner.h"
#include "SceneCanvas.h"
#include "ScapeLookAndFeel.h"

using namespace juce;
using namespace scape;

ImageOutliner::ImageOutliner()
{
    setWantsKeyboardFocus (true);

    for (int t = 0; t < kNumElementTypes; ++t)
        typeBox.addItem (getElementInfo (t).name, t + 1);
    typeBox.onChange = [this] { type = typeBox.getSelectedId() - 1; repaint(); };
    addAndMakeVisible (typeBox);

    TextButton* modes[] = { &freehandButton, &ellipseButton, &rectangleButton, &wholeButton };
    for (int i = 0; i < 4; ++i)
    {
        modes[i]->setRadioGroupId (2001);
        modes[i]->setClickingTogglesState (true);
        modes[i]->setColour (TextButton::buttonOnColourId, ScapeColours::accent.withAlpha (0.35f));
        modes[i]->onClick = [this, i] { setShape (i); };
        addAndMakeVisible (modes[i]);
    }
    freehandButton.setTooltip ("Draw around the part you want, like a lasso");
    ellipseButton.setTooltip ("Drag out an ellipse (great for faces and round objects)");
    rectangleButton.setTooltip ("Drag out a rectangle");
    wholeButton.setTooltip ("Use the entire image");

    chooseButton.onClick = [this] { if (onChooseFile) onChooseFile (type); };
    cancelButton.onClick = [this] { if (onCancel) onCancel(); };
    applyButton.setColour (TextButton::buttonColourId, Colour (0xff2f6f5a));
    applyButton.onClick = [this] { if (onApply) onApply (type, skin); };

    for (auto* b : { &chooseButton, &cancelButton, &applyButton })
        addAndMakeVisible (b);
}

void ImageOutliner::open (int elementType, const ElementSkin& s)
{
    skin = s;
    skin.glyph = {};
    skin.encodedSource = s.encodedSource;
    type = jlimit (0, kNumElementTypes - 1, elementType);
    typeBox.setSelectedId (type + 1, dontSendNotification);

    TextButton* modes[] = { &freehandButton, &ellipseButton, &rectangleButton, &wholeButton };
    modes[jlimit (0, 3, skin.shape)]->setToggleState (true, dontSendNotification);

    resized();
    updatePreview();
    setVisible (true);
    toFront (true);
    grabKeyboardFocus();
}

void ImageOutliner::setShape (int shape)
{
    if (shape != skin.shape)
    {
        skin.shape = shape;
        skin.outline.clear();
        updatePreview();
        repaint();
    }
}

void ImageOutliner::updatePreview()
{
    preview = SkinTools::scaledHighQuality (SkinTools::renderGlyph (skin), 320);
    repaint (previewBox);
}

Point<float> ImageOutliner::toNormalised (Point<float> p) const noexcept
{
    return { jlimit (0.0f, 1.0f, (p.x - imageArea.getX()) / jmax (1.0f, imageArea.getWidth())),
             jlimit (0.0f, 1.0f, (p.y - imageArea.getY()) / jmax (1.0f, imageArea.getHeight())) };
}

void ImageOutliner::resized()
{
    panel = getLocalBounds().reduced (jmax (24, getWidth() / 18), jmax (20, getHeight() / 18));
    auto area = panel.reduced (20);

    auto top = area.removeFromTop (34);
    top.removeFromLeft (250);
    typeBox.setBounds (top.removeFromLeft (140));
    top.removeFromLeft (24);
    for (auto* b : { &freehandButton, &ellipseButton, &rectangleButton, &wholeButton })
    {
        b->setBounds (top.removeFromLeft (104));
        top.removeFromLeft (6);
    }

    auto bottom = area.removeFromBottom (36);
    applyButton.setBounds (bottom.removeFromRight (120));
    bottom.removeFromRight (8);
    cancelButton.setBounds (bottom.removeFromRight (90));
    chooseButton.setBounds (bottom.removeFromLeft (190));

    area.removeFromTop (30);
    area.removeFromBottom (12);
    previewBox = area.removeFromRight (jmin (240, area.getWidth() / 4));
    area.removeFromRight (16);
    imageBox = area;

    if (skin.source.isValid())
    {
        const auto placement = RectanglePlacement (RectanglePlacement::centred);
        imageArea = placement.appliedTo (skin.source.getBounds().toFloat(), imageBox.toFloat());
    }
}

void ImageOutliner::paint (Graphics& g)
{
    g.fillAll (Colours::black.withAlpha (0.72f));

    g.setColour (ScapeColours::panel);
    g.fillRoundedRectangle (panel.toFloat(), 14.0f);
    g.setColour (ScapeColours::panelEdge);
    g.drawRoundedRectangle (panel.toFloat(), 14.0f, 1.0f);

    auto header = panel.reduced (20).removeFromTop (34);
    g.setColour (ScapeColours::text);
    g.setFont (FontOptions (17.0f, Font::bold));
    g.drawText ("Outline your image for", header.removeFromLeft (240), Justification::centredLeft);

    // Image with everything outside the outline dimmed.
    g.setColour (Colours::black.withAlpha (0.4f));
    g.fillRect (imageBox);
    if (skin.source.isValid())
    {
        g.setImageResamplingQuality (Graphics::mediumResamplingQuality);
        g.setOpacity (1.0f);   // images are drawn with the current colour's alpha
        g.drawImage (skin.source, imageArea);

        // Dim everything, then redraw the selected part at full brightness.
        const auto path = SkinTools::outlinePath (skin, imageArea);
        g.setColour (Colours::black.withAlpha (0.6f));
        g.fillRect (imageArea);
        {
            Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (path);
            g.setOpacity (1.0f);
            g.drawImage (skin.source, imageArea);
        }

        g.setColour (Colours::white);
        g.strokePath (path, PathStrokeType (2.0f));
        Path dashed;
        const float dashes[] = { 6.0f, 6.0f };
        PathStrokeType (2.0f).createDashedStroke (dashed, path, dashes, 2);
        g.setColour (Colour (getElementInfo (type).colour));
        g.fillPath (dashed);
    }

    // Instructions under the toolbar
    static const char* hints[] = {
        "Draw around the part of the picture you want to use. Release to close the outline.",
        "Drag to draw an ellipse around the part you want.",
        "Drag to draw a rectangle around the part you want.",
        "The whole image will be used.",
    };
    g.setColour (ScapeColours::textDim);
    g.setFont (FontOptions (13.0f));
    g.drawText (hints[jlimit (0, 3, skin.shape)], imageBox.withHeight (22).translated (0, -25), Justification::centredLeft);

    // Preview: how it will look on the canvas
    g.setColour (ScapeColours::background);
    g.fillRoundedRectangle (previewBox.toFloat(), 12.0f);
    g.setColour (ScapeColours::textDim);
    g.drawText ("On the canvas", previewBox.withHeight (30), Justification::centred);
    if (preview.isValid())
    {
        const float r = (float) jmin (previewBox.getWidth(), previewBox.getHeight()) * 0.22f;
        drawElementGlyph (g, type, previewBox.toFloat().getCentre(), r, 0.5f, 0.0f, false, &preview);
    }
    g.setColour (ScapeColours::textDim);
    g.drawFittedText (String (getElementInfo (type).name) + "\n" + getElementInfo (type).description,
                      previewBox.reduced (12).removeFromBottom (70), Justification::centred, 4);
}

void ImageOutliner::mouseDown (const MouseEvent& e)
{
    if (! imageArea.contains (e.position) || skin.shape == ElementSkin::wholeImage)
        return;

    drawing = true;
    const auto n = toNormalised (e.position);
    skin.outline.clear();
    skin.outline.push_back (n);
    if (skin.shape != ElementSkin::freehand)
        skin.outline.push_back (n);
    repaint();
}

void ImageOutliner::mouseDrag (const MouseEvent& e)
{
    if (! drawing) return;
    const auto n = toNormalised (e.position);

    if (skin.shape == ElementSkin::freehand)
    {
        const auto last = skin.outline.back();
        const float minStep = 2.0f / jmax (1.0f, imageArea.getWidth());
        if (last.getDistanceFrom (n) > minStep)
            skin.outline.push_back (n);
    }
    else if (skin.outline.size() >= 2)
    {
        skin.outline[1] = n;
    }
    repaint (imageBox);
}

void ImageOutliner::mouseUp (const MouseEvent&)
{
    if (! drawing) return;
    drawing = false;
    updatePreview();
    repaint();
}

bool ImageOutliner::keyPressed (const KeyPress& key)
{
    if (key == KeyPress::escapeKey) { if (onCancel) onCancel(); return true; }
    if (key == KeyPress::returnKey) { if (onApply) onApply (type, skin); return true; }
    return false;
}
