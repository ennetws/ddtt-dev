function [M, shapes, clustering, centers] = demo(filename,shapes_filename)
    
    % Load correspondence results
    [M, shapes]=loadCorrespondence(filename,shapes_filename);

    % Convert distance to similairty
    s = 1-M;

    % Preference 
    p = median(1-M);

    % Cluster
    clusters = apcluster(s,p);

    % Visualize
    centers = unique(clusters);
    k = length(centers);
    clustering = cell(1,k);

    % Collect shape indicies per cluster
    max_cluster_length = 0;
    for i=1:k
        cluster_center = centers(i);
        ele_indices = find(clusters == cluster_center);
        w = length(ele_indices);
        clustering{i}.elements = cell(1,w);
        for j=1:w
            % Index of shape belonging to cluster 'i'
            idx = ele_indices(j);        
            clustering{i}.elements{j} = idx;
        end
        
        max_cluster_length = max([max_cluster_length, size(ele_indices)]);
    end

    % Setup thumbnails for clusters
    cols = cell(k,1);
    for c=1:k 
        cur_cluster = clustering{c}.elements;

        idx_representative = centers(c);
        class_representative = makeThumb(shapes{ idx_representative }.thumb);
        length_cur_cluster = length(cur_cluster);
        
        row = cell(max_cluster_length,1);
        row{1} = class_representative;
        
        u = 2;
        for i=1:length_cur_cluster
            cur_idx = cur_cluster{i};
            cluster_element = shapes{ cur_idx };
            img = makeThumb(cluster_element.thumb);
            row{u} = img;
            if cur_idx == idx_representative
                continue
            end
            u = u + 1;
        end
        
        cols{c} = row;
    end

    % 
    for iRow = 1:k
        for iCol = 1:max_cluster_length 
            %subplot(k, max_cluster_length, iCol + ((iRow-1)*max_cluster_length));
            subaxis(k, max_cluster_length, iCol + ((iRow-1)*max_cluster_length), 'SpacingVert',0);
            r = cols{iRow};
            c = r{iCol};
            imshow(c);
            axis off; 
        end 
    end 
    fclose('all');
end

function thumb_img = makeThumb(filename)
    thumb_img = imresize(imread(filename), [NaN 128]);
end
