import QtQuick
import QtQuick.VirtualKeyboard
import QtQuick.VirtualKeyboard.Components

KeyboardLayout {
    inputMode: InputEngine.InputMode.Latin
    keyWeight: 160

    KeyboardRow {
        Key { key: Qt.Key_1; text: "1"; alternativeKeys: ["!"] }
        Key { key: Qt.Key_2; text: "2"; alternativeKeys: ["@"] }
        Key { key: Qt.Key_3; text: "3"; alternativeKeys: ["#"] }
        Key { key: Qt.Key_4; text: "4"; alternativeKeys: ["$"] }
        Key { key: Qt.Key_5; text: "5"; alternativeKeys: ["%"] }
        FillerKey { weight: 420 }
        Key { key: Qt.Key_6; text: "6"; alternativeKeys: ["^"] }
        Key { key: Qt.Key_7; text: "7"; alternativeKeys: ["&"] }
        Key { key: Qt.Key_8; text: "8"; alternativeKeys: ["*"] }
        Key { key: Qt.Key_9; text: "9"; alternativeKeys: ["("] }
        Key { key: Qt.Key_0; text: "0"; alternativeKeys: [")"] }
        BackspaceKey { weight: 220 }
    }

    KeyboardRow {
        Key { key: Qt.Key_Q; text: "q"; alternativeKeys: ["1", "!"] }
        Key { key: Qt.Key_W; text: "w"; alternativeKeys: ["2", "@"] }
        Key { key: Qt.Key_E; text: "e"; alternativeKeys: ["3", "#"] }
        Key { key: Qt.Key_R; text: "r"; alternativeKeys: ["4", "$"] }
        Key { key: Qt.Key_T; text: "t"; alternativeKeys: ["5", "%"] }
        FillerKey { weight: 420 }
        Key { key: Qt.Key_Y; text: "y"; alternativeKeys: ["6", "^"] }
        Key { key: Qt.Key_U; text: "u"; alternativeKeys: ["7", "&"] }
        Key { key: Qt.Key_I; text: "i"; alternativeKeys: ["8", "*"] }
        Key { key: Qt.Key_O; text: "o"; alternativeKeys: ["9", "("] }
        Key { key: Qt.Key_P; text: "p"; alternativeKeys: ["0", ")"] }
        Key { key: Qt.Key_Minus; text: "-"; alternativeKeys: ["_", "="] }
    }

    KeyboardRow {
        Key { key: Qt.Key_A; text: "a" }
        Key { key: Qt.Key_S; text: "s" }
        Key { key: Qt.Key_D; text: "d" }
        Key { key: Qt.Key_F; text: "f" }
        Key { key: Qt.Key_G; text: "g" }
        FillerKey { weight: 420 }
        Key { key: Qt.Key_H; text: "h" }
        Key { key: Qt.Key_J; text: "j" }
        Key { key: Qt.Key_K; text: "k" }
        Key { key: Qt.Key_L; text: "l" }
        Key { key: Qt.Key_Apostrophe; text: "'"; alternativeKeys: ["\"", ";", ":"] }
        EnterKey { weight: 240 }
    }

    KeyboardRow {
        ShiftKey { weight: 210 }
        Key { key: Qt.Key_Z; text: "z" }
        Key { key: Qt.Key_X; text: "x" }
        Key { key: Qt.Key_C; text: "c" }
        Key { key: Qt.Key_V; text: "v" }
        Key { key: Qt.Key_B; text: "b" }
        FillerKey { weight: 420 }
        Key { key: Qt.Key_N; text: "n" }
        Key { key: Qt.Key_M; text: "m" }
        Key { key: Qt.Key_Comma; text: ","; alternativeKeys: ["<", "?"] }
        Key { key: Qt.Key_Period; text: "."; alternativeKeys: [">", "/"] }
        ShiftKey { weight: 210 }
    }

    KeyboardRow {
        SymbolModeKey { weight: 190 }
        ChangeLanguageKey { weight: 140 }
        Key { key: Qt.Key_Tab; displayText: "Tab"; functionKey: true; weight: 150 }
        SpaceKey { weight: 380 }
        FillerKey { weight: 420 }
        SpaceKey { weight: 380 }
        Key { key: Qt.Key_Left; displayText: "◀"; functionKey: true; weight: 140 }
        Key { key: Qt.Key_Right; displayText: "▶"; functionKey: true; weight: 140 }
        HideKeyboardKey { weight: 180 }
    }
}
