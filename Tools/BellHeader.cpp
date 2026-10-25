#include <juce_data_structures/juce_data_structures.h>
#include <iostream>
juce::var asJson(const juce::ValueTree& tree) {
    auto* object = new juce::DynamicObject;
    object->setProperty("type", tree.getType().toString());
    auto* properties = new juce::DynamicObject;
    for (int i = 0; i < tree.getNumProperties(); ++i) {
        const auto name = tree.getPropertyName(i);
        properties->setProperty(name, tree.getProperty(name));
    }
    object->setProperty("properties", juce::var(properties));
    juce::Array<juce::var> children;
    for (const auto child : tree) children.add(asJson(child));
    object->setProperty("children", children);
    return juce::var(object);
}
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    auto input = juce::File(juce::String::fromUTF8(argv[1])).createInputStream();
    if (!input) return 3;
    const auto tree = juce::ValueTree::readFromStream(*input);
    if (!tree.isValid()) return 4;
    const auto xml = tree.createXml();
    if (!xml || !xml->writeTo(juce::File(juce::String::fromUTF8(argv[2])))) return 5;
    const auto output = juce::File(juce::String::fromUTF8(argv[2])).getParentDirectory();
    if (!juce::File(juce::String::fromUTF8(argv[2])).withFileExtension("json").replaceWithText(juce::JSON::toString(asJson(tree)))) return 8;
    for (const auto node : tree.getChildWithName("PoolData")) {
        juce::MemoryBlock data;
        if (!data.fromBase64Encoding(node.getProperty("Data").toString())) return 6;
        if (!output.getChildFile(node.getType().toString() + ".pool").replaceWithData(data.getData(), data.getSize())) return 7;
    }
    std::cout << tree.getType().toString() << '\n';
}
