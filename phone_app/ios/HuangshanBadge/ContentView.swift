import PhotosUI
import SwiftUI

struct ContentView: View {
    @EnvironmentObject private var watch: WatchBLEManager
    @State private var selectedPhoto: PhotosPickerItem?
    @State private var isShowingDevices = false

    var body: some View {
        NavigationStack {
            VStack(spacing: 18) {
                preview
                controls
                status
                Spacer()
            }
            .padding()
            .navigationTitle("电子吧唧")
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button("连接") { isShowingDevices = true }
                        .disabled(watch.isBluetoothUnavailable)
                }
            }
            .sheet(isPresented: $isShowingDevices) { devicePicker }
            .task(id: selectedPhoto) { await loadPhoto() }
            .alert("传图失败", isPresented: Binding(
                get: { watch.errorMessage != nil },
                set: { if !$0 { watch.errorMessage = nil } }
            )) {
                Button("确定", role: .cancel) { watch.errorMessage = nil }
            } message: {
                Text(watch.errorMessage ?? "")
            }
        }
    }

    private var preview: some View {
        ZStack {
            Color.black
            if let image = watch.previewImage {
                Image(uiImage: image)
                    .resizable()
                    .interpolation(.high)
                    .scaledToFill()
            } else {
                Text("选择一张图片")
                    .foregroundStyle(.secondary)
            }
            if watch.isUploading {
                VStack(spacing: 6) {
                    Text("\(Int(watch.progress * 100))%")
                        .font(.system(size: 36, weight: .bold, design: .rounded))
                    Text(watch.transferStatus)
                        .font(.footnote)
                }
                .foregroundStyle(.white)
            }
        }
        .frame(maxWidth: 360)
        .aspectRatio(1, contentMode: .fit)
        .clipShape(RoundedRectangle(cornerRadius: 8))
    }

    private var controls: some View {
        VStack(spacing: 10) {
            PhotosPicker(selection: $selectedPhoto, matching: .images) {
                Label("选择图片", systemImage: "photo")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.bordered)

            Button {
                Task { await watch.uploadPreparedImage() }
            } label: {
                Label("发送到手表", systemImage: "arrow.up.circle.fill")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)
            .disabled(!watch.canUpload)

            HStack {
                Button("刷新状态") { watch.requestStatus() }
                Button("取消传输") { watch.cancelTransfer() }
                Button("清除图片", role: .destructive) { watch.clearBadge() }
            }
            .buttonStyle(.bordered)
            .disabled(!watch.isConnected)
        }
    }

    private var status: some View {
        List {
            LabeledContent("设备", value: watch.connectedName)
            LabeledContent("图片", value: watch.imageStatus)
            LabeledContent("传输", value: watch.transferStatus)
        }
        .frame(height: 170)
        .listStyle(.plain)
    }

    private var devicePicker: some View {
        NavigationStack {
            List(watch.discoveredDevices, id: \.identifier) { peripheral in
                Button(peripheral.name ?? "Huangshan Watch") {
                    watch.connect(to: peripheral)
                    isShowingDevices = false
                }
            }
            .overlay {
                if watch.discoveredDevices.isEmpty {
                    ContentUnavailableView("正在搜索手表", systemImage: "dot.radiowaves.left.and.right")
                }
            }
            .navigationTitle("选择手表")
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button("关闭") { isShowingDevices = false }
                }
            }
            .onAppear { watch.startScanning() }
            .onDisappear { watch.stopScanning() }
        }
    }

    private func loadPhoto() async {
        guard let selectedPhoto,
              let data = try? await selectedPhoto.loadTransferable(type: Data.self) else { return }
        watch.prepareImage(from: data)
    }
}
