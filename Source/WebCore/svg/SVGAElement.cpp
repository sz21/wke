/*
 * Copyright (C) 2004, 2005, 2008 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2004, 2005, 2007 Rob Buis <buis@kde.org>
 * Copyright (C) 2007 Eric Seidel <eric@webkit.org>
 * Copyright (C) 2010 Apple Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"

#if ENABLE(SVG)
#include "SVGAElement.h"

#include "Attr.h"
#include "Attribute.h"
#include "Document.h"
#include "EventHandler.h"
#include "EventNames.h"
#include "Frame.h"
#include "FrameLoader.h"
#include "FrameLoaderTypes.h"
#include "HTMLAnchorElement.h"
#include "HTMLParserIdioms.h"
#include "KeyboardEvent.h"
#include "MouseEvent.h"
#include "PlatformMouseEvent.h"
#include "RenderSVGInline.h"
#include "RenderSVGTransformableContainer.h"
#include "ResourceRequest.h"
#include "SVGElementInstance.h"
#include "SVGNames.h"
#include "SVGSMILElement.h"
#include "XLinkNames.h"

namespace WebCore {

// Animated property definitions
DEFINE_ANIMATED_STRING(SVGAElement, SVGNames::targetAttr, SVGTarget, svgTarget)
DEFINE_ANIMATED_STRING(SVGAElement, XLinkNames::hrefAttr, Href, href)
DEFINE_ANIMATED_BOOLEAN(SVGAElement, SVGNames::externalResourcesRequiredAttr, ExternalResourcesRequired, externalResourcesRequired)

BEGIN_REGISTER_ANIMATED_PROPERTIES(SVGAElement)
     REGISTER_LOCAL_ANIMATED_PROPERTY(svgTarget)
     REGISTER_LOCAL_ANIMATED_PROPERTY(href)
     REGISTER_LOCAL_ANIMATED_PROPERTY(externalResourcesRequired)
     REGISTER_PARENT_ANIMATED_PROPERTIES(SVGStyledTransformableElement)
     REGISTER_PARENT_ANIMATED_PROPERTIES(SVGTests)
END_REGISTER_ANIMATED_PROPERTIES

inline SVGAElement::SVGAElement(const QualifiedName& tagName, Document* document)
    : SVGStyledTransformableElement(tagName, document)
{
    ASSERT(hasTagName(SVGNames::aTag));
    registerAnimatedPropertiesForSVGAElement();
}

PassRefPtr<SVGAElement> SVGAElement::create(const QualifiedName& tagName, Document* document)
{
    return adoptRef(new SVGAElement(tagName, document));
}

String SVGAElement::title() const
{
    // If the xlink:title is set (non-empty string), use it.
    const AtomicString& title = fastGetAttribute(XLinkNames::titleAttr);
    if (!title.isEmpty())
        return title;

    // Otherwise, use the title of this element.
    return SVGStyledElement::title();
}

bool SVGAElement::isSupportedAttribute(const QualifiedName& attrName)
{
    DEFINE_STATIC_LOCAL(HashSet<QualifiedName>, supportedAttributes, ());
    if (supportedAttributes.isEmpty()) {
        SVGURIReference::addSupportedAttributes(supportedAttributes);
        SVGTests::addSupportedAttributes(supportedAttributes);
        SVGLangSpace::addSupportedAttributes(supportedAttributes);
        SVGExternalResourcesRequired::addSupportedAttributes(supportedAttributes);
        supportedAttributes.add(SVGNames::targetAttr);
    }
    return supportedAttributes.contains(attrName);
}

void SVGAElement::parseMappedAttribute(Attribute* attr)
{
    if (!isSupportedAttribute(attr->name())) {
        SVGStyledTransformableElement::parseMappedAttribute(attr);
        return;
    }

    if (attr->name() == SVGNames::targetAttr) {
        setSVGTargetBaseValue(attr->value());
        return;
    }

    if (SVGURIReference::parseMappedAttribute(attr))
        return;
    if (SVGTests::parseMappedAttribute(attr))
        return;
    if (SVGLangSpace::parseMappedAttribute(attr))
        return;
    if (SVGExternalResourcesRequired::parseMappedAttribute(attr))
        return;

    ASSERT_NOT_REACHED();
}

void SVGAElement::svgAttributeChanged(const QualifiedName& attrName)
{
    if (!isSupportedAttribute(attrName)) {
        SVGStyledTransformableElement::svgAttributeChanged(attrName);
        return;
    }

    SVGElementInstance::InvalidationGuard invalidationGuard(this);

    // Unlike other SVG*Element classes, SVGAElement only listens to SVGURIReference changes
    // as none of the other properties changes the linking behaviour for our <a> element.
    if (SVGURIReference::isKnownAttribute(attrName)) {
        bool wasLink = isLink();
        setIsLink(!href().isNull());

        if (wasLink != isLink())
            setNeedsStyleRecalc();
    }
}

RenderObject* SVGAElement::createRenderer(RenderArena* arena, RenderStyle*)
{
    if (static_cast<SVGElement*>(parentNode())->isTextContent())
        return new (arena) RenderSVGInline(this);

    return new (arena) RenderSVGTransformableContainer(this);
}

void SVGAElement::defaultEventHandler(Event* event)
{
    if (isLink()) {
        if (focused() && isEnterKeyKeydownEvent(event)) {
            event->setDefaultHandled();
            dispatchSimulatedClick(event);
            return;
        }

        if (isLinkClick(event)) {
            String url = stripLeadingAndTrailingHTMLSpaces(href());

            if (url[0] == '#') {
                Element* targetElement = treeScope()->getElementById(url.substring(1));
#if ENABLE(SVG_ANIMATION)
                if (SVGSMILElement::isSMILElement(targetElement)) {
                    static_cast<SVGSMILElement*>(targetElement)->beginByLinkActivation();
                    event->setDefaultHandled();
                    return;
                }
#endif
                // Only allow navigation to internal <view> anchors.
                if (targetElement && !targetElement->hasTagName(SVGNames::viewTag))
                    return;
            }

            // FIXME: Why does the SVG anchor element have this special logic
            // for middle click that the HTML anchor element does not have?
            // Making a middle click open a link in a new window or tab is
            // properly handled at the client level, not inside WebKit; this
            // code should be deleted.
            String target = isMiddleMouseButtonEvent(event) ? "_blank" : this->target();

            // FIXME: It's not clear why setting target to "_self" is ever
            // helpful.
            if (target.isEmpty())
                target = (fastGetAttribute(XLinkNames::showAttr) == "new") ? "_blank" : "_self";

            handleLinkClick(event, document(), url, target);
            return;
        }
    }

    SVGStyledTransformableElement::defaultEventHandler(event);
}

bool SVGAElement::supportsFocus() const
{
    if (rendererIsEditable())
        return SVGStyledTransformableElement::supportsFocus();
    return true;
}

bool SVGAElement::isFocusable() const
{
    if (renderer() && renderer()->absoluteClippedOverflowRect().isEmpty())
        return false;
    
    return SVGElement::isFocusable();
}

bool SVGAElement::isMouseFocusable() const
{
    return false;
}

bool SVGAElement::isKeyboardFocusable(KeyboardEvent* event) const
{
    if (!isFocusable())
        return false;
    
    if (!document()->frame())
        return false;
    
    return document()->frame()->eventHandler()->tabsToLinks(event);
}

bool SVGAElement::childShouldCreateRenderer(Node* child) const
{
    // http://www.w3.org/2003/01/REC-SVG11-20030114-errata#linking-text-environment
    // The 'a' element may contain any element that its parent may contain, except itself.
    if (child->hasTagName(SVGNames::aTag))
        return false;
    if (parentNode() && parentNode()->isSVGElement())
        return parentNode()->childShouldCreateRenderer(child);

    return SVGElement::childShouldCreateRenderer(child);
}

} // namespace WebCore

#endif // ENABLE(SVG)
