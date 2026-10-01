pragma Singleton
import QtQuick 2.15

QtObject {
    id: root

    // =========================================================================
    // COLOR PALETTE (Modern GitHub / Linear / Dark Cyber Aesthetic)
    // =========================================================================

    // Background Layers
    readonly property color bgApp: "#0D1117"           // Deep obsidian canvas
    readonly property color bgSidebar: "#161B22"       // Sidebar & elevated panels
    readonly property color bgCard: "#21262D"          // Card background
    readonly property color bgCardHover: "#292E36"     // Hovered card
    readonly property color bgInput: "#0D1117"         // Text field background
    readonly property color bgSubtle: "#1C2128"        // Subtle background tint
    readonly property color bgPill: "#161B22"          // Pill / Chip background

    // Borders & Dividers
    readonly property color borderMuted: "#30363D"     // Subtle separator border
    readonly property color borderActive: "#388BFD"    // Active selection border
    readonly property color borderFocus: "#58A6FF"     // Focus glow border
    readonly property color borderSubtle: "#21262D"    // Very subtle border

    // Text & Foregrounds
    readonly property color textPrimary: "#F0F6FC"     // Crisp readable white
    readonly property color textSecondary: "#8B949E"   // Muted grey
    readonly property color textTertiary: "#6E7681"    // Dim helper text
    readonly property color textDisabled: "#484F58"    // Disabled text
    readonly property color textOnAccent: "#FFFFFF"    // Text over bright accent

    // Accents & Semantics
    readonly property color accent: "#58A6FF"          // Electric Blue (Primary)
    readonly property color accentHover: "#79C0FF"     // Lighter hover blue
    readonly property color accentSubtle: "#1F6FEB26"  // Low opacity blue fill

    readonly property color success: "#3FB950"         // Mint Green
    readonly property color successSubtle: "#23863626" // Low opacity green fill
    readonly property color warning: "#D29922"         // Amber / Gold
    readonly property color warningSubtle: "#9E6A0326" // Low opacity amber fill
    readonly property color danger: "#F85149"          // Crimson / Red
    readonly property color dangerSubtle: "#DA363326"  // Low opacity red fill

    readonly property color reasoning: "#BC8CFF"       // Deep Purple for Agent reasoning
    readonly property color reasoningSubtle: "#8957E526"

    readonly property color retrieval: "#39C5BB"       // Teal / Cyan for Vector / RAG
    readonly property color retrievalSubtle: "#1B7C7526"

    // =========================================================================
    // TYPOGRAPHY (Reliable Windows / System Fallbacks)
    // =========================================================================
    readonly property string fontUi: "Segoe UI, -apple-system, sans-serif"
    readonly property string fontMono: "Cascadia Code, Consolas, monospace"

    readonly property int fontSizeXs: 10
    readonly property int fontSizeSm: 11
    readonly property int fontSizeBase: 13
    readonly property int fontSizeMd: 14
    readonly property int fontSizeLg: 16
    readonly property int fontSizeXl: 18

    // =========================================================================
    // METRICS & GEOMETRY
    // =========================================================================
    readonly property int radiusXs: 4
    readonly property int radiusSm: 6
    readonly property int radiusMd: 8
    readonly property int radiusLg: 12
    readonly property int radiusPill: 20

    readonly property int spacingXs: 4
    readonly property int spacingSm: 8
    readonly property int spacingMd: 12
    readonly property int spacingLg: 16
    readonly property int spacingXl: 24

    readonly property int animFast: 120
    readonly property int animNormal: 220
}
