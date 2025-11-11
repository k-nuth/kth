import json
from copy import deepcopy

def main():
    matrix = {"config": []}

    with open("platform.json", "r") as platform_file:
        platform_data = json.load(platform_file)

        with open("build_order.json", "r") as read_file:
            data = json.load(read_file)
            order_arr = data["order"]
            for level in order_arr:
                # print(f"Level: {level}")
                for reference in level:
                    # print(f"reference: {reference}")
                    # print(f"reference: {reference['ref']}")

                    # Detect if this is a build requirement (tool_requires)
                    is_build = False
                    if 'packages' in reference and reference['packages']:
                        for pkg_list in reference['packages']:
                            for pkg in pkg_list:
                                if isinstance(pkg, dict) and pkg.get('context') == 'build':
                                    is_build = True
                                    break

                    for platform in platform_data['config']:
                        platform_final = deepcopy(platform)
                        platform_final["name"] = f'{platform_final["name"]} - {reference["ref"]}'
                        platform_final["reference"] = reference["ref"]
                        platform_final["context"] = "build" if is_build else "host"
                        # print(f"reference: {platform['reference']}")
                        matrix["config"].append(deepcopy(platform_final))

            if len(matrix["config"]) == 0:
                # Create a dummy entry with minimal required fields when no dependencies to build
                # This prevents workflow errors when os field is accessed
                matrix["config"].append({
                    "reference": "null",
                    "os": "ubuntu-latest",
                    "context": "host"
                })

    print(matrix)
    with open("matrix.json", "w") as write_file:
        json.dump(matrix, write_file)
        write_file.write("\n")

if __name__ == "__main__":
    main()
