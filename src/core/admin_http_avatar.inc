std::string appearance_json(const avatar::AvatarAppearance& appearance) {
    std::ostringstream out;
    out << "{\"user_id\":\"" << json_escape(appearance.user_id)
        << "\",\"revision\":" << appearance.revision
        << ",\"avatar_height\":" << appearance.avatar_height
        << ",\"visual_params\":\"" << json_escape(appearance.visual_params_csv)
        << "\",\"updated_unix\":" << appearance.updated_unix
        << ",\"wearables\":[";
    for (std::size_t i = 0; i < appearance.wearables.size(); ++i) {
        if (i) out << ',';
        const auto& wearable = appearance.wearables[i];
        out << "{\"slot\":\"" << json_escape(wearable.slot)
            << "\",\"item_id\":\"" << json_escape(wearable.item_id)
            << "\",\"asset_id\":\"" << json_escape(wearable.asset_id) << "\"}";
    }
    out << "],\"attachments\":[";
    for (std::size_t i = 0; i < appearance.attachments.size(); ++i) {
        if (i) out << ',';
        const auto& attachment = appearance.attachments[i];
        out << "{\"point\":\"" << json_escape(attachment.point)
            << "\",\"item_id\":\"" << json_escape(attachment.item_id)
            << "\",\"asset_id\":\"" << json_escape(attachment.asset_id) << "\"}";
    }
    out << "]}";
    return out.str();
}
