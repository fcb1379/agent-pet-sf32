import SwiftUI

@main
struct HuangshanBadgeApp: App {
    @StateObject private var watch = WatchBLEManager()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(watch)
        }
    }
}
